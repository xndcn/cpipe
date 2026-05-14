vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ultravideo/kvazaar
    REF "v${VERSION}"
    SHA512 fdb26de258e923c0cfa6741421689fc1d77c9b37040776e25d28d148d5254968e72d9716c26df45c3150afcac33a8fd61625488aa951183a1a1a347cc6f53fa7
    HEAD_REF master
)

vcpkg_check_linkage(ONLY_DYNAMIC_LIBRARY)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    DISABLE_PARALLEL_CONFIGURE
    OPTIONS
        -DBUILD_TESTS=OFF
        -DBUILD_KVAZAAR_BINARY=OFF
        -DUSE_CRYPTO=OFF
)

vcpkg_cmake_install()
vcpkg_fixup_pkgconfig()
vcpkg_copy_pdbs()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
