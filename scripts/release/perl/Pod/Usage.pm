# Minimal stand-in for Pod::Usage (absent from Git for Windows' perl); OpenSSL's generated
# configdata.pm imports pod2usage() for its --help path only.
package Pod::Usage;
use Exporter 'import';
our @EXPORT = qw(pod2usage);
sub pod2usage { print STDERR "usage: see configdata.pm\n"; exit 2; }
1;
