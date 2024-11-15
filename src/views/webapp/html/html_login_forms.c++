#include "html_login_forms.h++"
#include "html_form_widgets.h++"

using std::optional, std::string_view, fmt::operator""_cf; // NOLINT

namespace Ludwig {

#define HONEYPOT_FIELD \
  R"(<label for="username" class="a11y"><span>Don't type here unless you're a bot</span>)" \
  R"(<input type="text" name="username" id="username" tabindex="-1" autocomplete="off"></label>)"

void html_login_form(ResponseWriter& r, optional<string_view> error) noexcept {
  r.write_fmt(
    R"(<main><form class="form form-page" method="post" action="/login">{})"
    HONEYPOT_FIELD
    HTML_FIELD("actual_username", "Username or email", "text", "")
    HTML_FIELD("password", "Password", "password", "")
    HTML_CHECKBOX("remember", "Remember me", "")
    R"(<input type="submit" value="Login"></form></main>)"_cf,
    error_banner(error)
  );
}

void html_sidebar_login_form(ResponseWriter& r) noexcept {
  r.write(
    R"(<form method="post" action="/login" id="login-form">)"
    HONEYPOT_FIELD
    R"(<label for="actual_username"><span class="a11y">Username or email</span><input type="text" name="actual_username" id="actual_username" placeholder="Username or email"></label>)"
    R"(<label for="password"><span class="a11y">Password</span><input type="password" name="password" id="password" placeholder="Password"></label>)"
    R"(<label for="remember"><input type="checkbox" name="remember" id="remember"> Remember me</label>)"
    R"(<input type="submit" value="Login" class="big-button"></form>)"
  );
}

void html_register_form(ResponseWriter& r, const SiteDetail* site, optional<string_view> error) noexcept {
  r.write_fmt(
    R"(<main><form data-component="Form" class="form form-page" method="post" action="/register">{})"_cf,
    error_banner(error)
  );
  r.write(
    HONEYPOT_FIELD
    HTML_FIELD("actual_username", "Username", "text", R"( required pattern=")" USERNAME_REGEX_SRC R"(")")
    HTML_FIELD("email", "Email address", "email", " required")
    HTML_FIELD("password", "Password", "password", " required")
    HTML_FIELD("confirm_password", "Confirm password", "password", " required")
  );
  if (site->registration_invite_required) {
    r.write(HTML_FIELD("invite_code", "Invite code", "text", R"( required pattern=")" INVITE_CODE_REGEX_SRC R"(")"));
  }
  if (site->registration_application_required) {
    r.write_fmt(
      R"(<label for="application_reason"><span>{}</span><textarea name="application_reason" required autocomplete="off"></textarea></label>)"_cf,
      Escape{site->application_question.value_or("Why do you want to join?")}
    );
  }
  r.write(R"(<input type="submit" value="Register"></form></main>)");
}

}