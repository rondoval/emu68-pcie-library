// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2014 Google, Inc
 *
 * (C) Copyright 2001 Sysgo Real-Time Solutions, GmbH <www.elinos.com>
 * Andreas Heppel <aheppel@sysgo.de>
 *
 * (C) Copyright 2002, 2003
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 */
#include <exec/types.h>

#include <pci.h>

const char *pci_class_str(UBYTE class)
{
	switch (class) {
	case PCI_CLASS_NOT_DEFINED:
		return "Build before PCI Rev2.0";
		break;
	case PCI_BASE_CLASS_STORAGE:
		return "Mass storage controller";
		break;
	case PCI_BASE_CLASS_NETWORK:
		return "Network controller";
		break;
	case PCI_BASE_CLASS_DISPLAY:
		return "Display controller";
		break;
	case PCI_BASE_CLASS_MULTIMEDIA:
		return "Multimedia device";
		break;
	case PCI_BASE_CLASS_MEMORY:
		return "Memory controller";
		break;
	case PCI_BASE_CLASS_BRIDGE:
		return "Bridge device";
		break;
	case PCI_BASE_CLASS_COMMUNICATION:
		return "Simple comm. controller";
		break;
	case PCI_BASE_CLASS_SYSTEM:
		return "Base system peripheral";
		break;
	case PCI_BASE_CLASS_INPUT:
		return "Input device";
		break;
	case PCI_BASE_CLASS_DOCKING:
		return "Docking station";
		break;
	case PCI_BASE_CLASS_PROCESSOR:
		return "Processor";
		break;
	case PCI_BASE_CLASS_SERIAL:
		return "Serial bus controller";
		break;
	case PCI_BASE_CLASS_INTELLIGENT:
		return "Intelligent controller";
		break;
	case PCI_BASE_CLASS_SATELLITE:
		return "Satellite controller";
		break;
	case PCI_BASE_CLASS_CRYPT:
		return "Cryptographic device";
		break;
	case PCI_BASE_CLASS_SIGNAL_PROCESSING:
		return "DSP";
		break;
	case PCI_CLASS_OTHERS:
		return "Does not fit any class";
		break;
	default:
	return  "???";
		break;
	};
}
