# Overlay for libunwind 1.8.3.
#
# vcpkg's builtin port downloads the GitHub "archive/v1.8.3.tar.gz" auto-generated
# tarball, whose content/hash changed upstream (CI: "unexpected hash"). Use the
# immutable release asset instead, with its official SHA512.
vcpkg_download_distfile(ARCHIVE
    URLS "https://github.com/libunwind/libunwind/releases/download/v${VERSION}/libunwind-${VERSION}.tar.gz"
    FILENAME "libunwind-${VERSION}.tar.gz"
    SHA512 732c9b74635cf4e65bc90fd5b9a4b90fe5968c7fcce6cc704295038f0d41b848f78a59d725e813072e78df72ff8fc77001096abdfb9ef1c73e3bd82433987679
)
vcpkg_extract_source_archive(SOURCE_PATH
    ARCHIVE "${ARCHIVE}"
    PATCHES "${CMAKE_CURRENT_LIST_DIR}/liblzma.diff"
)

vcpkg_find_acquire_program(PKGCONFIG)
vcpkg_configure_make(
    SOURCE_PATH "${SOURCE_PATH}"
    AUTOCONFIG
    OPTIONS
        --disable-documentation
        --disable-tests
        --disable-zlibdebuginfo
        --enable-minidebuginfo
)
vcpkg_install_make()
vcpkg_fixup_pkgconfig()
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
