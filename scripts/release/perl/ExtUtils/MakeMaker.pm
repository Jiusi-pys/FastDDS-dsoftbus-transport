# Minimal stand-in for ExtUtils::MakeMaker (absent from Git for Windows' perl). OpenSSL's
# Configure reaches it only through IPC::Cmd::can_run(), which calls MM->maybe_command().
package ExtUtils::MakeMaker;
our $VERSION = '7.70';
package MM;
sub maybe_command { my ($self, $file) = @_; return $file if -f $file && -x _; return; }
1;
