# Собираем все .c и .cpp файлы из директории src
file(GLOB_RECURSE c_srcs "src/*.c")
file(GLOB_RECURSE cpp_srcs "src/*.cpp")

# Объединяем списки
set(srcs ${c_srcs} ${cpp_srcs})

idf_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio
    PRIV_REQUIRES nvs_flash esp_wifi esp_netif esp_http_server esp_event esp_http_client mbedtls
)

# Включаем C++17 только для C++ файлов
set_source_files_properties(
    ${cpp_srcs}
    PROPERTIES COMPILE_FLAGS "-std=gnu++17"
)