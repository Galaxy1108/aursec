# Maintainer: aursec contributors
# Contributor: 

pkgname=aursec
pkgver=0.3.0
pkgrel=1
pkgdesc="yay wrapper - AI review of PKGBUILD before every install/upgrade"
arch=('x86_64')
url="https://github.com/Galaxy1108/aursec"
license=('MIT')
depends=('yay' 'curl' 'nlohmann-json' 'openssl')
makedepends=('cmake' 'glib2')
optdepends=('libsecret: 系统密钥环支持'
            'libarchive: deep 审查级别（解压 AUR 源码快照）')
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
    ./build/test_aursec
}

package() {
    cd "$srcdir/$pkgname"
    install -Dm755 build/aursec "$pkgdir/usr/bin/aursec"
}
