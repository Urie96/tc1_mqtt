{
  pkgs,
  micoder,
  ...
}:
let
  mico-build = micoder.packages.${pkgs.stdenv.hostPlatform.system}.mico-build;
in
{
  packages = [
    mico-build
    pkgs.gcc-arm-embedded
  ];

  scripts.build.exec = ''
    ${mico-build}/bin/mico-build TC1@MK3031@moc
  '';

  scripts.build_and_flash.exec = ''
    sudo ${mico-build}/bin/mico-build TC1@MK3031@moc download JTAG=jlink_swd run
  '';
}
