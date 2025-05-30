

# SPDX-FileCopyrightText: Copyright 2019-2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: BSD-3-Clause


.PHONY: help

LOCKHAMMER_DIR=benchmarks/lockhammer

help:
	@echo
	@echo "This Makefile passes to $(LOCKHAMMER_DIR)/Makefile"
	@echo
	@echo "try:"
	@echo
	@echo "   make -j 8 allvariants"
	@echo

%::
	$(MAKE) -C $(LOCKHAMMER_DIR) $(MAKEFLAGS) $(MAKECMDGOALS)
