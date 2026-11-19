# The Oric Atmos machine (platform independent; host build in host/)
idf_component_register(SRCS "oric.c"
                            "via.c"
                            "video.c"
                            "psg.c"
                            "keyboard.c"
                            "tape.c"
                            "microdisc.c"
                            "wd1793.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "."
                       REQUIRES m6502)

# Runs together with the CPU core in the emulation loop
target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
