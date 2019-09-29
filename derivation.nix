{ stdenv, lib, plan9port, darwin, ... }:

stdenv.mkDerivation {
  pname = "osxsnarf";
  version = "0.1.0";
  src = ./.;

  buildInputs = [ plan9port darwin.apple_sdk.frameworks.Carbon ];
  makeFlags = [ "prefix=${placeholder "out"}" ];

  meta = with lib; {
    description = "A Plan 9-inspired way to share your OS X clipboard.";
    homepage = https://github.com/eraserhd/osxsnarf;
    license = licenses.unlicense;
    platforms = platforms.darwin;
    maintainers = [ maintainers.eraserhd ];
  };
}
