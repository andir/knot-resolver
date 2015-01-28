# Knot DNS Resolver

[![Build Status](https://travis-ci.org/CZNIC-Labs/knot-resolver.svg?branch=master)](https://travis-ci.org/CZNIC-Labs/knot-resolver)

The Knot DNS Resolver is a minimalistic caching resolver implementation. The project provides both a resolver
library and a small daemon. Modular architecture of the library keeps the core tiny and efficient, and provides
a state-machine like API for extensions. There are three built-in modules: *iterator*, *cache* and *stats*,
but each module can be flipped on and off.

### Try it out?

The Knot DNS Resolver is currently in an early development phase, you shouldn't put it in the production right away.

### Docker image

This is simple and doesn't require any dependencies or system modifications, just run:

```
$ docker run cznic/knot-resolver
```

See the build page https://registry.hub.docker.com/u/cznic/knot-resolver for more information and options.
You can hack on the container by changing the container entrypoint to shell like:

```
$ docker run -it --entrypoint=/bin/bash cznic/knot-resolver
```

### Building from sources 

The Knot DNS Resolver depends on the development version of the Knot DNS library, and a reasonably recent version of `libuv`.
Several dependencies may not be in the packages yet, the script pulls and installs all dependencies in a chroot.

```
$ FAKEROOT="/tmp/resolver-depends"
$ ./scripts/build-depends.sh ${FAKEROOT} 
$ export LDFLAGS="-L${FAKEROOT}/lib"
$ export PKG_CONFIG_PATH="${FAKEROOT}/lib/pkgconfig"
$ ./configure --enable-integration-tests
$ autoreconf -if
$ make
$ make check
```

### Running

There is a separate resolver library in the `lib` directory, and a minimalistic daemon in
the `daemon` directory. The daemon accepts a few CLI parameters, and there's no support for configuration
right now.

```
$ ./daemon/kresolved -h
$ ./daemon/kresolved -a 127.0.0.1#53
```
