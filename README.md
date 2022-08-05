# tiny-c-libs

A collection of tiny C libraries that may be useful to some.

The following libraries are included:

- libdefer ([defer.h](), [defer.c]())
- libexcept ([except.h](), [except.c]())
- libvec ([vec.h](), [vec.c]())
- libobj ([obj.h](), [obj.c]())

## Notes

libdefer and libexcept need `-lpthread`. libdefer additionally needs `-finstrument-functions` and can utilize libunwind by defining `DEFER_HAVE_LIBUNWIND`
