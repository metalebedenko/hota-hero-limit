#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
using Bytes = std::vector<std::uint8_t>;
constexpr wchar_t original_name[] = L"h3hota HD.exe";
constexpr wchar_t modded_name[] = L"h3hota HeroLimit.exe";
constexpr char original_import[] = "VERSION.dll";
constexpr char modded_import[] = "HeroLim.dll";
constexpr char executable_code_sha256[] = "eb96d2ece7f047587ca430cbb421db678fad71c49929818ede6a3b5a5d35ce62";
constexpr char hota_sha256[] = "1fa9ad209e756d55e9578b290c4fe6c9ba030d3b52af2085e530416a61ac10c2";

std::optional<Bytes> read_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return std::nullopt;
    const auto size = input.tellg();
    if (size < 0 || size > 32 * 1024 * 1024) return std::nullopt;
    Bytes bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    if (size && !input.read(reinterpret_cast<char*>(bytes.data()), size)) return std::nullopt;
    return bytes;
}

bool matches_sha256(std::span<std::uint8_t> bytes, const char* expected) {
    BCRYPT_ALG_HANDLE algorithm{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
        return false;
    }
    BCRYPT_HASH_HANDLE hash{};
    auto status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
    std::array<std::uint8_t, 32> digest{};
    if (status >= 0) {
        status = BCryptHashData(hash, bytes.data(), static_cast<ULONG>(bytes.size()), 0);
        if (status >= 0) status = BCryptFinishHash(hash, digest.data(), digest.size(), 0);
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) return false;
    constexpr char hex[] = "0123456789abcdef";
    for (std::size_t i = 0; i < digest.size(); ++i) {
        if (expected[2 * i] != hex[digest[i] >> 4] ||
            expected[2 * i + 1] != hex[digest[i] & 0xf]) return false;
    }
    return true;
}

template<class T> bool read_at(const Bytes& file, std::size_t offset, T& value) {
    if (offset > file.size() || sizeof(T) > file.size() - offset) return false;
    std::memcpy(&value, file.data() + offset, sizeof(T));
    return true;
}

bool patch_import(Bytes& image) {
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS32 pe{};
    if (!read_at(image, 0, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 ||
        !read_at(image, dos.e_lfanew, pe) || pe.Signature != IMAGE_NT_SIGNATURE ||
        pe.FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        pe.FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER32) ||
        pe.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        pe.OptionalHeader.ImageBase != 0x400000 ||
        pe.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_IMPORT) return false;

    std::vector<IMAGE_SECTION_HEADER> sections(pe.FileHeader.NumberOfSections);
    const std::size_t first_section = static_cast<std::size_t>(dos.e_lfanew) +
        offsetof(IMAGE_NT_HEADERS32, OptionalHeader) + pe.FileHeader.SizeOfOptionalHeader;
    bool verified = false;
    for (std::size_t i = 0; i < sections.size(); ++i) {
        auto& section = sections[i];
        if (!read_at(image, first_section + i * sizeof(section), section) ||
            section.PointerToRawData > image.size() ||
            section.SizeOfRawData > image.size() - section.PointerToRawData) return false;
        if (std::memcmp(section.Name, ".text\0\0", sizeof(section.Name)) == 0) {
            verified = section.VirtualAddress == 0x1000 && matches_sha256(
                std::span(image).subspan(section.PointerToRawData, section.SizeOfRawData),
                executable_code_sha256);
        }
    }
    if (!verified) return false;
    const auto file_offset = [&](DWORD rva, std::size_t size) -> std::optional<std::size_t> {
        for (const auto& section : sections) {
            if (rva < section.VirtualAddress) continue;
            const auto distance = rva - section.VirtualAddress;
            if (distance <= section.SizeOfRawData && size <= section.SizeOfRawData - distance) {
                return section.PointerToRawData + distance;
            }
        }
        return std::nullopt;
    };
    const auto imports = pe.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (std::size_t i = 0; i < imports.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR); ++i) {
        const auto rva = static_cast<std::uint64_t>(imports.VirtualAddress) + i * sizeof(IMAGE_IMPORT_DESCRIPTOR);
        if (rva > MAXDWORD) return false;
        IMAGE_IMPORT_DESCRIPTOR descriptor{};
        const auto offset = file_offset(static_cast<DWORD>(rva), sizeof(descriptor));
        if (!offset || !read_at(image, *offset, descriptor) || !descriptor.Name) break;
        const auto name = file_offset(descriptor.Name, sizeof(original_import));
        if (name && descriptor.OriginalFirstThunk &&
            std::memcmp(image.data() + *name, original_import, sizeof(original_import)) == 0) {
            std::memcpy(image.data() + *name, modded_import, sizeof(modded_import));
            std::fill_n(image.begin() + *offset + offsetof(IMAGE_IMPORT_DESCRIPTOR, TimeDateStamp),
                        sizeof(descriptor.TimeDateStamp), 0);
            return true;
        }
    }
    return false;
}

std::span<const std::uint8_t> payload(unsigned id) {
    const HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!resource) return {};
    const HGLOBAL handle = LoadResource(nullptr, resource);
    if (!handle) return {};
    return {static_cast<const std::uint8_t*>(LockResource(handle)), SizeofResource(nullptr, resource)};
}

bool same_file(const fs::path& path, std::span<const std::uint8_t> content) {
    const auto existing = read_file(path);
    return existing && std::ranges::equal(*existing, content);
}

bool install_new_file(const fs::path& path, std::span<const std::uint8_t> content, bool keep_existing = false) {
    const auto exists = [&] {
        const auto flags = GetFileAttributesW(path.c_str());
        return flags != INVALID_FILE_ATTRIBUTES && (flags & FILE_ATTRIBUTE_DIRECTORY) == 0;
    };
    if (exists()) return keep_existing || same_file(path, content);
    std::array<wchar_t, MAX_PATH> temporary{};
    if (!GetTempFileNameW(path.parent_path().c_str(), L"hll", 0, temporary.data())) return false;
    const HANDLE file = CreateFileW(temporary.data(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD count = 0;
    const bool written = file != INVALID_HANDLE_VALUE &&
        WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &count, nullptr) &&
        count == content.size() && FlushFileBuffers(file);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (written && MoveFileExW(temporary.data(), path.c_str(), MOVEFILE_WRITE_THROUGH)) return true;
    DeleteFileW(temporary.data());
    return exists() && (keep_existing || same_file(path, content));
}

std::optional<std::wstring> install(const fs::path& source) {
    if (_wcsicmp(source.filename().c_str(), original_name) != 0) {
        return L"Выберите h3hota HD.exe в папке установленной игры.";
    }
    const auto directory = source.parent_path();
    auto executable = read_file(source);
    auto hota = read_file(directory / L"HotA.dll");
    if (!executable || !hota) return L"Не удалось прочитать h3hota HD.exe или HotA.dll.";
    if (!matches_sha256(*hota, hota_sha256) || !patch_import(*executable)) {
        return L"Файлы движка отличаются от поддерживаемой сборки HotA 1.8.1.";
    }

    const auto module = payload(101);
    const auto config = payload(102);
    if (module.empty() || config.empty()) return L"Файлы внутри установщика повреждены.";
    if (!install_new_file(directory / L"HeroLim.dll", module) ||
        !install_new_file(directory / L"HeroLimit.ini", config, true) ||
        !install_new_file(directory / modded_name, *executable)) {
        return L"Не удалось добавить файлы в папку HotA. Проверьте право записи и совпадение "
               L"уже существующей установки Hero Limit с этим выпуском.";
    }
    return std::nullopt;
}

fs::path choose_game() {
    std::array<wchar_t, MAX_PATH> selected{};
    constexpr wchar_t filter[] = L"HotA HD\0h3hota HD.exe\0\0";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = selected.data();
    dialog.nMaxFile = selected.size();
    dialog.lpstrTitle = L"Выберите h3hota HD.exe в папке игры";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    return GetOpenFileNameW(&dialog) ? fs::path(selected.data()) : fs::path();
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const bool quiet = argc == 3 && std::wcscmp(argv[1], L"--install") == 0;
    const auto source = quiet ? fs::path(argv[2]) : choose_game();
    if (argv) LocalFree(argv);
    if (source.empty()) return 0;

    const auto result = install(source);
    if (!quiet) {
        constexpr wchar_t title[] = L"Hero Limit для HotA";
        if (result) {
            MessageBoxW(nullptr, result->c_str(), title, MB_OK | MB_ICONERROR);
        } else {
            MessageBoxW(nullptr, L"Готово. Для игры с лимитом откройте h3hota HeroLimit.exe "
                                 L"в папке HotA.", title, MB_OK | MB_ICONINFORMATION);
        }
    }
    return result ? 1 : 0;
}
