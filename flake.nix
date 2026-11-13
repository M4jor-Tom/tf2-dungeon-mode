{
  description = "TF2 Dungeon Mode — Source SDK 2013 mod dev shell (podman + Steam Runtime sniper)";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAll = f: nixpkgs.lib.genAttrs systems (system: f (import nixpkgs { inherit system; config.allowUnfree = true; }));
    in {
      devShells = forAll (pkgs: {
        default = pkgs.mkShell {
          packages = with pkgs; [ podman git python3 ccache ninja steamcmd ];
          shellHook = ''
            # Rootless podman on a NixOS host without the podman module: supply a
            # signature policy that trusts Valve's registry and docker.io.
            mkdir -p "$HOME/.config/containers" "$HOME/.ccache"
            if ! grep -q steamos "$HOME/.config/containers/policy.json" 2>/dev/null; then
              cat > "$HOME/.config/containers/policy.json" <<'JSON'
{ "default": [ { "type": "reject" } ],
  "transports": { "docker": {
    "docker.io": [ { "type": "insecureAcceptAnything" } ],
    "registry.gitlab.steamos.cloud": [ { "type": "insecureAcceptAnything" } ] } } }
JSON
            fi
            [ -f "$HOME/.config/containers/registries.conf" ] || \
              echo 'unqualified-search-registries = ["docker.io"]' > "$HOME/.config/containers/registries.conf"
            echo "tf2-dungeon-mode: build with  (cd src && bash ./buildallprojects)"
          '';
        };
      });
    };
}
