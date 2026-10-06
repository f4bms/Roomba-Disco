# Poky agrega "pulseaudio" a DISTRO_FEATURES por backfill y con eso mpg123 se compila
# contra PulseAudio, que arrastra libpulse, libsndfile y las bibliotecas de X11. Se
# deja solo la salida ALSA, que es la que usa la biblioteca (mpg123 -o alsa).
PACKAGECONFIG:remove = "pulseaudio"
PACKAGECONFIG:append = " alsa"
