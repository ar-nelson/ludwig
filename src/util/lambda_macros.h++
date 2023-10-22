#pragma once

// courtesy of https://www.reddit.com/r/cpp/comments/umqi08/is_short_lambda_syntax_on_the_c_radar/i866gst/
//
// "Thanks I hate it" - /u/nintendiator2

#define λARG        [[maybe_unused]] auto &&
#define λBODY(EXPR) noexcept(noexcept(EXPR)) -> decltype(EXPR) { return (EXPR); }

#define λ(EXPR)     []()                                 λBODY(EXPR) // nullary
#define λx(EXPR)    []( λARG x )                         λBODY(EXPR) // unary
#define λxy(EXPR)   []( λARG x, λARG y )                 λBODY(EXPR) // binary
#define λxyz(EXPR)  []( λARG x, λARG y, λARG z )         λBODY(EXPR) // ternary
#define λxyzw(EXPR) []( λARG x, λARG y, λARG z, λARG w ) λBODY(EXPR) // quaternary
