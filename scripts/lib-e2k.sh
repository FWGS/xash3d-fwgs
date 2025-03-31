declare -A E2K_CROSS_COMPILER_URL E2K_CROSS_COMPILER_PATH E2K_PACKAGES_URLS

# NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE
# NOTE                                                                       NOTE
# NOTE  The cross-compiler is officially distributed on dev.mcst.ru website. NOTE
# NOTE   setwd.ws is OpenE2K's unofficial mirror for Elbrus dev community.   NOTE
# NOTE Do NOT hammer down the server with your CI/CD, AI and other bullshit! NOTE
# NOTE           Cache the archives on YOUR OWN INFRASTRUCTURE!              NOTE
# NOTE                                                                       NOTE
# NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE
E2K_CROSS_COMPILER_URL[e2k-4c]="https://setwd.ws/sp/1.27/1.27.21/cross-sp-rel-1.27.21.e2k-v3.5.10_64.tgz"
E2K_CROSS_COMPILER_URL[e2k-1c]="https://setwd.ws/sp/1.27/1.27.21/cross-sp-rel-1.27.21.e2k-v4.1c%2B.5.10_64.tgz"
E2K_CROSS_COMPILER_URL[e2k-8c]="https://setwd.ws/sp/1.27/1.27.21/cross-sp-rel-1.27.21.e2k-v4.5.10_64.tgz"
E2K_CROSS_COMPILER_URL[e2k-8c2]="https://setwd.ws/sp/1.27/1.27.21/cross-sp-rel-1.27.21.e2k-v5.5.10_64.tgz"
E2K_CROSS_COMPILER_URL[e2k-16c]="https://setwd.ws/sp/1.27/1.27.21/cross-sp-rel-1.27.21.e2k-v6.5.10-e16c_64.tgz"
E2K_CROSS_COMPILER_URL[e2k-2c3]="https://setwd.ws/sp/1.27/1.27.21/cross-sp-rel-1.27.21.e2k-v6.5.10-e2c3_64.tgz"

# TODO: fill in the gaps
E2K_CROSS_COMPILER_PATH[e2k-8c]="/opt/mcst/lcc-1.27.21.e2k-v4.5.10/"

# TODO: split by some character, as bash can't assign lists to array members
E2K_PACKAGES_URLS[e2k-4c]="https://setwd.ws/osl/8.2/pool/main/e2k-4c/SDL2_2.30.0-vd8u4_e2k-4c.deb"
E2K_PACKAGES_URLS[e2k-8c]="https://setwd.ws/osl/8.2/pool/main/e2k-8c/SDL2_2.30.0-vd8u4_e2k-8c.deb"
E2K_PACKAGES_URLS[e2k-8c2]="https://setwd.ws/osl/8.2/pool/main/e2k-8c2/SDL2_2.30.0-vd8u4_e2k-8c2.deb"
E2K_PACKAGES_URLS[e2k-16c]="https://setwd.ws/osl/8.2/pool/main/e2k-16c/SDL2_2.30.0-vd8u4_e2k-16c.deb"
