CC := m68k-amigaos-gcc
INCLUDE := -Iinclude
CFLAGS := -m68040 -O2 -fomit-frame-pointer -MMD -MP -Wall -Wno-unused-function -Wno-shift-count-overflow -DDEBUG $(INCLUDE)
LDFLAGS := -s
# -nostdlib 
# -nostartfiles
OBJDIR := Build
TARGET := pcie_list
OBJS := pcie_list.o devtree.o pcie_brcmstb.o pci_probe.o pci_auto.o pci_capability.o pci_io.o pci_lookup.o pci_util.o pci_bar.o mbox.o msg.o

all: $(OBJDIR) $(OBJDIR)/$(TARGET)

$(OBJDIR):
	@mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

$(OBJDIR)/$(TARGET): $(addprefix $(OBJDIR)/,$(OBJS))
	$(CC) $(foreach f,$(OBJS),$(OBJDIR)/$(f)) $(LDFLAGS) -o $@

clean:
	@rm -rf $(OBJDIR)

-include $(addprefix $(OBJDIR)/,$(OBJS:.o=.d))
