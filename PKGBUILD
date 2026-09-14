# Maintainer: Juanjo <tu-email@example.com>
# Contributor: Alex <itzAlex@github>

pkgname=chatterino7-git
_pkgname=chatterino7
pkgver=7.5.5.r0.g81e4e1c4f
pkgrel=1
pkgdesc="Chat client for Twitch (Chatterino Homies - SevenTV v7.5.5 base)"
arch=('x86_64')
url="https://github.com/Tankeeee2/Chatterino-Homies-Linux"
license=('MIT')
depends=(
    'boost-libs'
    'openssl'
    'qt6-base'
    'qt6-5compat'
    'qt6-svg'
    'qt6-imageformats'
    'libnotify'
    'gcc-libs'
)
makedepends=(
    'git'
    'cmake'
    'boost'
    'qt6-tools'
    'rapidjson'
)
optdepends=(
    'qt6-wayland: Wayland support'
)
provides=("${_pkgname}=${pkgver}")
conflicts=("${_pkgname}" 'chatterino2' 'chatterino2-bin' 'chatterino2-git')
source=("${_pkgname}::git+https://github.com/Tankeeee2/Chatterino-Homies-Linux.git")
sha256sums=('SKIP')

pkgver() {
    cd "${_pkgname}"
    printf "%s.r%s.g%s" "$(grep -Po '(?<=VERSION )\d+\.\d+\.\d+' CMakeLists.txt | head -1)" \
        "$(git rev-list --count HEAD)" \
        "$(git rev-parse --short HEAD)"
}

prepare() {
    cd "${_pkgname}"
    git submodule update --init --recursive
}

build() {
    cd "${_pkgname}"
    cmake -B build \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DBUILD_WITH_QTKEYCHAIN=OFF \
        -DBUILD_TESTS=OFF \
        -DBUILD_BENCHMARKS=OFF \
        -DCMAKE_BUILD_TYPE=Release
    cmake --build build
}

package() {
    cd "${_pkgname}"

    # Install binary
    install -Dm755 build/bin/chatterino "${pkgdir}/usr/bin/chatterino"

    # Install desktop file
    install -Dm644 resources/com.chatterino.chatterino.desktop \
        "${pkgdir}/usr/share/applications/com.chatterino.chatterino.desktop"

    # Install icon
    install -Dm644 resources/icon.png \
        "${pkgdir}/usr/share/icons/hicolor/256x256/apps/com.chatterino.chatterino.png"

    # Install license
    install -Dm644 LICENSE "${pkgdir}/usr/share/licenses/${pkgname}/LICENSE"
}