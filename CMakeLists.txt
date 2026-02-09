
set(srcs
    "src/uffs_badblock.c"
    "src/uffs_blockinfo.c"
    "src/uffs_buf.c"
    "src/uffs_crc.c"
    "src/uffs_debug.c"
    "src/uffs_device.c"
    "src/uffs_ecc.c"
    "src/uffs_fd.c"
    "src/uffs_find.c"
    "src/uffs_flash.c"
    "src/uffs_fs.c"
    "src/uffs_init.c"
    "src/uffs_mem.c"
    "src/uffs_mtb.c"
    "src/uffs_pool.c"
    "src/uffs_public.c"
    "src/uffs_tree.c"
    "src/uffs_utils.c"
    "src/uffs_version.c"
    "port/uffs_port.c"
)



list(APPEND srcs
    "port/esp_spi_nand_common.c"
    "port/esp_spi_nand_winbond.c"
    "port/esp_spi_nand_micron.c"
    "port/esp_spi_nand_alliance.c"
    "port/esp_spi_nand_zetta.c"
    "port/esp_spi_nand_xtx.c"
    "port/esp_spi_nand_gd.c"
    "port/esp_spi_nand.c"
)

set(priv_reqs log freertos)
set(pub_reqs)

if(NOT IDF_TARGET STREQUAL "linux")
    list(APPEND pub_reqs spi_flash driver)
else()
    list(APPEND pub_reqs mock_driver)
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include" "port"
                       PRIV_REQUIRES ${priv_reqs}
                       REQUIRES ${pub_reqs})
