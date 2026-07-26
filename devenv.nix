{
  pkgs,
  micoder,
  ...
}:
let
  mico-build = micoder.packages.${pkgs.stdenv.hostPlatform.system}.mico-build;
  micoder-path = toString micoder.packages.${pkgs.stdenv.hostPlatform.system}.micoder;
in
{
  packages = [
    mico-build
    pkgs.gcc-arm-embedded
  ];

  env.HOST_OS = "Linux64";
  env.TOOLS_ROOT = "${micoder-path}";
  env.OPENOCD_PATH = "${micoder-path}/OpenOCD/";
  env.OPENOCD_FULL_NAME = "${micoder-path}/OpenOCD/Linux64/openocd_mico";
  env.KILL_OPENOCD = "pkill -f openocd_mico 2>/dev/null; true";
  env.PYTHON = "${pkgs.python3}/bin/python3";

  scripts.build.exec = ''
    make -f ./mico-os/makefiles/Makefile TC1@MK3031@moc
  '';

  scripts.build_and_flash.exec = ''
    sudo -E make -f ./mico-os/makefiles/Makefile TC1@MK3031@moc download JTAG=jlink_swd run
  '';
}
