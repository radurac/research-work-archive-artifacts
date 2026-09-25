{
  imports = [
    ../modules/hardware/supermicro-x12spw-tf.nix
    ../modules/nfs/client.nix
    ../modules/dpdk.nix
    ../modules/vfio/iommu-intel.nix
    ../modules/monitoring/fpga-dashboard/switch-collector.nix
  ];

  powerManagement.enable = true;
  powerManagement.cpuFreqGovernor = "performance";

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

  systemd.network.ignorePci = [ 
    "0000:00:1c.0"
    "0000:00:1c.1"
    "0000:01:00.0"
    "0000:01:00.1"

  ];

  virtualisation.vfio.devices = [
     "8086:1563"
  ];

  networking.hostName = "christina";

  simd.arch = "icelake-server";

  system.stateVersion = "21.11";
}

