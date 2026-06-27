# Maintainer: install_aur contributors
# Contributor: 

pkgname=install_aur
pkgver=0.1.0
pkgrel=1
pkgdesc="yay wrapper - AI review of PKGBUILD before every install/upgrade"
arch=('x86_64')
url="https://github.com/user/install_aur"
license=('MIT')
depends=('yay' 'curl' 'nlohmann-json')
makedepends=('cmake')
source=("$pkgname::git+file://${startdir}")
sha256sums=('SKIP')

build() {
    cd "$srcdir/$pkgname"
    cmake -B build \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DBUILD_TESTS=OFF \
        -DCMAKE_BUILD_TYPE=Release
    cmake --build build
}

check() {
    cd "$srcdir/$pkgname"
    ./build/test_install_aur
}

package() {
    cd "$srcdir/$pkgname"
    install -Dm755 build/install_aur "$pkgdir/usr/bin/install_aur"
}
