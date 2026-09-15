# La imagen no trae DISTRO_FEATURES "alsa" ni "pulseaudio", así que la receta
# stock de mpg123 no compila ningún backend de salida de audio real (queda
# solo con el módulo "dummy"). Se fuerza acá para no tocar DISTRO_FEATURES
# global (evita rebuild en cascada de otras recetas).
PACKAGECONFIG:append = " alsa"
