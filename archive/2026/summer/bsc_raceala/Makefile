#   loadgen     traffic-generator machine   needs DPDK
#   host-dpdk   host, DPDK variant          needs DPDK (with vhost)
#   host-xdp    host, AF_XDP variant        needs libxdp + libbpf, and clang for the eBPF objects
#   vm          inside the guest            no external dependencies
#   tools       anywhere                    no external dependencies

CC        ?= gcc
CLANG     ?= clang
PKGCONF   ?= pkg-config

OPT       ?= -O2
CFLAGS    ?= -Wall -Wextra -g $(OPT)

#   make vm SEND_HISTO=1
#   make vm PROCESSING_HISTO=1
ifeq ($(SEND_HISTO),1)
SEND_HISTO_DEF := -DSEND_HISTO
endif

ifeq ($(PROCESSING_HISTO),1)
PROCESSING_HISTO_DEF := -DPROCESSING_HISTO
endif

LOADGEN   := load-gen
DPDKHC    := dpdk-host-cache
XDPHC     := af_xdp-host-cache
VMDIR     := host-cache-vm
SNDIR     := shared-nothing-vm
CSDIR     := context_switch_estimation

DPDK_CFLAGS  = $(shell $(PKGCONF) --cflags libdpdk 2>/dev/null)
DPDK_LIBS    = $(shell $(PKGCONF) --libs   libdpdk 2>/dev/null)
XDP_CFLAGS   = $(shell $(PKGCONF) --cflags libxdp libbpf 2>/dev/null)
XDP_LIBS     = $(shell $(PKGCONF) --libs   libxdp libbpf 2>/dev/null || echo -lxdp -lbpf)

define need_pkg
@$(PKGCONF) --exists $(1) || { \
  echo "error: pkg-config cannot find '$(1)'."; \
  echo "       If it is installed in a non-standard prefix, set PKG_CONFIG_PATH, e.g."; \
  echo "       make $@ PKG_CONFIG_PATH=/usr/local/lib/x86_64-linux-gnu/pkgconfig"; \
  exit 1; }
endef

define need_clang
@command -v $(CLANG) >/dev/null || { \
  echo "error: clang not found; it is required to compile the eBPF objects."; exit 1; }
endef


.PHONY: help loadgen host-dpdk host-xdp bpf vm tools check dirs ivshmem clean

help:
	@echo "Targets (build only what the machine needs):"
	@echo "  make loadgen     $(LOADGEN)/dpdk_multiple_streams_server   [DPDK]"
	@echo "  make host-dpdk   $(DPDKHC)/dpdk_host_cache                 [DPDK]"
	@echo "  make host-xdp    $(XDPHC)/af_xdp_host_cache + eBPF objects [libxdp, clang]"
	@echo "  make bpf         eBPF objects only                         [clang]"
	@echo "  make vm          the four guest-side clients"
	@echo "  make tools       dedupe_test + context-switch microbenchmarks"
	@echo "  make check       build and run dedupe_test"
	@echo
	@echo "  make dirs        create histo directories"
	@echo "  make ivshmem     create /dev/shm/ivshmem at the right size"
	@echo "  make clean"
	@echo
	@echo "Variables: CC=   CLANG=   PKG_CONFIG_PATH="


loadgen: $(LOADGEN)/dpdk_multiple_streams_server

$(LOADGEN)/dpdk_multiple_streams_server: $(LOADGEN)/dpdk_multiple_streams_server.c \
		$(LOADGEN)/dpdk_prp_utils.h dpdk_common.h sv_frame.h time_utils.h measurement_constants.h
	$(call need_pkg,libdpdk)
	$(CC) $(CFLAGS) $(DPDK_CFLAGS) -o $@ $< $(DPDK_LIBS)


host-dpdk: $(DPDKHC)/dpdk_host_cache

$(DPDKHC)/dpdk_host_cache: $(DPDKHC)/dpdk_host_cache.c $(DPDKHC)/dedupe.h \
		dpdk_common.h ivshmem_host_utils.h sv_cache.h time_utils.h measurement_constants.h
	$(call need_pkg,libdpdk)
	$(CC) $(CFLAGS) $(DPDK_CFLAGS) -o $@ $< $(DPDK_LIBS)


host-xdp: $(XDPHC)/af_xdp_host_cache bpf

$(XDPHC)/af_xdp_host_cache: $(XDPHC)/af_xdp_host_cache.c \
		ivshmem_host_utils.h sv_cache.h time_utils.h measurement_constants.h
	$(call need_pkg,libxdp)
	$(CC) $(CFLAGS) $(XDP_CFLAGS) -o $@ $< $(XDP_LIBS) -lpthread

BPF_CFLAGS = -g -O2 -target bpf -D__TARGET_ARCH_x86 -Wall 

bpf: $(XDPHC)/xdp_dedupe_forward_xsk.o $(XDPHC)/tc_encaps.o $(XDPHC)/xdp_dedupe_forward.o

$(XDPHC)/xdp_dedupe_forward_xsk.o: $(XDPHC)/xdp_dedupe_forward_xsk.c
	$(call need_clang)
	$(CLANG) $(BPF_CFLAGS) -c $< -o $@

$(XDPHC)/tc_encaps.o: $(XDPHC)/tc_encaps.c 
	$(call need_clang)
	$(CLANG) $(BPF_CFLAGS) -c $< -o $@

$(XDPHC)/xdp_dedupe_forward.o: $(XDPHC)/xdp_dedupe_forward.c
	$(call need_clang)
	$(CLANG) $(BPF_CFLAGS) -c $< -o $@


VM_BINS := $(VMDIR)/client_ivshmem \
           $(VMDIR)/client_ivshmem_efd_sync \
           $(VMDIR)/client_ivshmem_efd_sync_opt \
           $(SNDIR)/client_multiple_streams

vm: $(VM_BINS)

VM_HDRS := $(VMDIR)/ivshmem_vm_utils.h $(VMDIR)/vm_measure.h $(VMDIR)/efd_read.h \
           sv_cache.h sv_frame.h net_utils.h time_utils.h measurement_constants.h

$(VMDIR)/client_ivshmem:              $(VMDIR)/client_ivshmem.c              $(VM_HDRS)
$(VMDIR)/client_ivshmem_efd_sync:     $(VMDIR)/client_ivshmem_efd_sync.c     $(VM_HDRS)
$(VMDIR)/client_ivshmem_efd_sync_opt: $(VMDIR)/client_ivshmem_efd_sync_opt.c $(VM_HDRS)

$(VM_BINS):
	$(CC) $(CFLAGS) $(SEND_HISTO_DEF) $(PROCESSING_HISTO_DEF) -o $@ $@.c -lpthread

$(SNDIR)/client_multiple_streams: $(SNDIR)/client_multiple_streams.c \
		sv_frame.h net_utils.h time_utils.h measurement_constants.h


CS_BINS := $(CSDIR)/2_proc_efd $(CSDIR)/2_proc_efd_histo

tools: $(DPDKHC)/dedupe_test $(CS_BINS)

$(DPDKHC)/dedupe_test: $(DPDKHC)/dedupe_test.c $(DPDKHC)/dedupe.h
	$(CC) $(CFLAGS) -o $@ $<

$(CSDIR)/2_proc_efd:       $(CSDIR)/2_proc_efd.c
$(CSDIR)/2_proc_efd_histo: $(CSDIR)/2_proc_efd_histo.c

$(CS_BINS):
	$(CC) $(CFLAGS) -o $@ $@.c -lpthread

check: $(DPDKHC)/dedupe_test
	@echo " dedupe regression tests "
	@./$(DPDKHC)/dedupe_test


dirs:
	mkdir -p $(VMDIR)/histograms

ivshmem:
	truncate -s 16M /dev/shm/ivshmem
	@ls -l /dev/shm/ivshmem

clean:
	$(RM) $(LOADGEN)/dpdk_multiple_streams_server \
	      $(DPDKHC)/dpdk_host_cache $(DPDKHC)/dedupe_test \
	      $(XDPHC)/af_xdp_host_cache $(XDPHC)/xdp_dedupe_forward_xsk.o $(XDPHC)/tc_encaps.o $(XDPHC)/xdp_dedupe_forward.o \
	      $(VM_BINS) $(CS_BINS)
