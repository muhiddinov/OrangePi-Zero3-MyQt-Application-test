################################################################################
#
# myqtapp
#
################################################################################

MYQTAPP_VERSION = 1.0
MYQTAPP_SITE = $(BR2_EXTERNAL_ORANGEPI_ZERO3_APP_PATH)/package/myqtapp/src
MYQTAPP_SITE_METHOD = local
MYQTAPP_LICENSE = Proprietary
MYQTAPP_DEPENDENCIES = qt5base qt5declarative qt5quickcontrols2 qt5multimedia

define MYQTAPP_CONFIGURE_CMDS
	(cd $(@D); $(TARGET_MAKE_ENV) $(HOST_DIR)/bin/qmake)
endef

define MYQTAPP_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)
endef

define MYQTAPP_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/myqtapp $(TARGET_DIR)/usr/bin/myqtapp
endef

define MYQTAPP_INSTALL_INIT_SYSV
	$(INSTALL) -D -m 0755 $(MYQTAPP_PKGDIR)/S99myqtapp $(TARGET_DIR)/etc/init.d/S99myqtapp
endef

$(eval $(generic-package))
