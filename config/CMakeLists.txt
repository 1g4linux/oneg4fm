# install default config files to /etc/xdg

configure_file(oneg4fm/default/settings.conf.in oneg4fm/default/settings.conf @ONLY)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/oneg4fm/default/settings.conf"
    "${CMAKE_CURRENT_SOURCE_DIR}/oneg4fm/schema.json"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/oneg4fm/default"
    COMPONENT Runtime
)
