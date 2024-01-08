#ifndef X
#  include <util/common.h++>
#  define XBEGIN(name) struct name {
#  define X(field, type) type field;
#  define XEND };
#endif

XBEGIN(JwtPayload)
X(sub, uint64_t)
X(iat, uint64_t)
X(exp, uint64_t)
XEND

#undef XBEGIN
#undef X
#undef XEND
