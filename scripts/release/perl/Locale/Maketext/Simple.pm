# Minimal stand-in for Locale::Maketext::Simple, which Git for Windows' perl omits but
# OpenSSL's Configure loads indirectly (IPC::Cmd -> Params::Check). Only loc()/loc_lang().
package Locale::Maketext::Simple;
sub import {
    my $caller = caller;
    no strict 'refs';
    *{"${caller}::loc"} = sub { my $s = shift; $s =~ s/\[_(\d+)\]/$_[$1-1]/g; $s };
    *{"${caller}::loc_lang"} = sub { };
}
1;
