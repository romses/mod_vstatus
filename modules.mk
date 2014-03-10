mod_vstatus.la: mod_vstatus.slo
	$(SH_LINK) -rpath $(libexecdir) -module -avoid-version  mod_vstatus.lo
DISTCLEAN_TARGETS = modules.mk
shared =  mod_vstatus.la
