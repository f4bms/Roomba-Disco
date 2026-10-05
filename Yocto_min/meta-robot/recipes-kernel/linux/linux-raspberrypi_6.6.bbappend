# EXTERNALSRC (ruta personal) va en tu conf/local.conf local, no acá.
KERNEL_DANGLING_FEATURES_WARN_ONLY = "1"
KERNEL_VERSION_SANITY_SKIP = "1"

# Tailscale (solo desarrollo, eliminar después): necesita /dev/net/tun
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI += "file://tun.cfg"
