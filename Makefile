CC := m68k-amigaos-gcc
INCLUDE := -Iinclude
CFLAGS := -m68040 -O2 -fomit-frame-pointer -MMD -MP -Wall -Wno-unused-function -DDEBUG $(INCLUDE)
OBJDIR := Build
TARGET := pcie_list
OBJS := pcie_list.o devtree.o pcie_brcmstb.o pci-uclass.o pci_auto.o

all: $(OBJDIR) $(OBJDIR)/$(TARGET)

$(OBJDIR):
	@mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

$(OBJDIR)/$(TARGET): $(addprefix $(OBJDIR)/,$(OBJS))
	$(CC) $(foreach f,$(OBJS),$(OBJDIR)/$(f)) -o $@

clean:
	@rm -rf $(OBJDIR)

-include $(addprefix $(OBJDIR)/,$(OBJS:.o=.d))
