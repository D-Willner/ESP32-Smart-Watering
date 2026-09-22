idf_component_register(SRCS "main.c" "watering.c" "measurements.c" "http_server.c"
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES "web/index.html" "web/config.html"
                    )
