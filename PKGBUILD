# Maintainer: aura contributors
# Contributor: 

pkgname=aura
pkgver=0.1.0
pkgrel=1
pkgdesc="yay wrapper - AI review of PKGBUILD before every install/upgrade"
arch=('x86_64')
url="https://github.com/user/aura"
license=('MIT')
depends=('yay' 'curl' 'nlohmann-json')
makedepends=('cmake')
source=("$pkgname::git+file://${startdir}")
sha256sums=('SKIP')

build() {
    cd "$srcdir/$pkgname"
    cmake -B build \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DBUILD_TESTS=ON \
        -DCMAKE_BUILD_TYPE=Release
    cmake --build build
}

check() {
    cd "$srcdir/$pkgname"
    ./build/test_aura
}

package() {
    cd "$srcdir/$pkgname"
    install -Dm755 build/aura "$pkgdir/usr/bin/aura"
}
