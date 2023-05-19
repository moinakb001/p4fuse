{ pkgs ? import <nixpkgs> {} }:



(pkgs.buildFHSUserEnv {
  name = "work";
  targetPkgs = pkgs: (with pkgs;
    [ 
#       (import ./cust.nix { inherit pkgs; })
      alsa-lib
      perl
      nss
      p4
      p4v
      expat
      fontconfig
      xorg.libxcb
      xorg.libX11
      xorg.libXcomposite
      xorg.libXdamage
      xorg.libXext
      xorg.libXfixes
      xorg.libXrender
      freetype
      nspr
      bash
      zsh
      libcgroup
      
    ]);
  profile = ''
    export P4USER=moinakb;
    export P4PORT=p4proxy-ngvpn03:2006;
    export P4CLIENT=linrs;
  '';
  runScript = "bash";
}).env
