# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(dynamic_web)

zephyr_linker_sources(SECTIONS sections-rom.ld)
zephyr_linker_section(NAME http_resource_desc_web_http_service KVMA RAM_REGION GROUP RODATA_REGION)

set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated)
generate_inc_file_for_target(app web/index.html ${gen_dir}/web_index_html.inc)
generate_inc_file_for_target(app web/styles.css ${gen_dir}/web_styles_css.inc)
generate_inc_file_for_target(app web/app.js ${gen_dir}/web_app_js.inc)
generate_inc_file_for_target(app web/vendor/bootstrap/css/bootstrap.min.css ${gen_dir}/web_bootstrap_min_css_gz.inc --gzip)
generate_inc_file_for_target(app web/vendor/bootstrap/js/bootstrap.bundle.min.js ${gen_dir}/web_bootstrap_bundle_min_js_gz.inc --gzip)

target_sources(app PRIVATE
  src/app_utils.c
  src/main.c
  src/wifi_service.c
  src/filesystem_service.c
  src/webserver_service.c
)
