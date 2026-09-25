{lib, pkgs, ...}:
{
  imports = [
    ../modules/hardware/supermicro-x12spw-tf.nix
    ../modules/nfs/client.nix
    ../modules/dpdk.nix
    ../modules/vfio/iommu-intel.nix
  ];
  
  powerManagement.enable = true;
  powerManagement.cpuFreqGovernor = "performance";

    boot.zfs.package = pkgs.zfs_unstable;

  boot.kernelParams = [
    "nosmt"
    "isolcpus=8-11"
    "nohz_full=8-11"
    "rcu_nocbs=8-11"
    "irqaffinity=0-7"
    "kthread_cpus=0-7"
    "intel_idle.max_cstate=0"
  ];
  
  systemd.settings.Manager = {
    CPUAffinity = "0-7";
  };
  
  systemd.user.extraConfig = ''
    [Manager]
    CPUAffinity=0-7
  '';

  boot.hugepages1GB.number = 20;

  virtualisation.libvirtd.enable = true;


  networking.hostName = "river";

  simd.arch = "icelake-server";

  system.stateVersion = "21.11";
}

