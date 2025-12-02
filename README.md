# aimage

## Modernization Covenant

This is a refactored and modernized edition of **aimage**, a tool to create aff-images. 

Key changes:

- ✅ Fixed errors caused by old libraries
- ✅ Fixed warnings caused by old-style coding
- ✅ Cleaned autoreconf rules
- ✅ OpenSSL latest version supported
- ✅ Refactored code to be used with latest gcc version

### Installation

#### Dependencies

Build dependencies:
```
gcc
```

Runtime dependencies:
```
afflib
openssl
readline
```

#### From source

```bash
git clone https://github.com/Obsidian-Covenant/aimage.git
cd aimage
autoreconf -i
./configure
make
sudo make install
```