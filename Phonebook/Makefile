include $(TOPDIR)/rules.mk

PKG_NAME:=aredn-phonebook
PKG_VERSION:=2.1.0
ifeq ($(BUILD_SHA),)
PKG_RELEASE:=1
else
PKG_RELEASE:=1-$(BUILD_SHA)
endif

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

define Package/aredn-phonebook
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=Telephony
  TITLE:=AREDN Phonebook
  URL:=https://github.com/dhamstack/AREDN-Phonebook
endef

define Package/aredn-phonebook/description
  AREDN Phonebook SIP Server with passive safety features
  for Amateur Radio Emergency Data Networks.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) -r ./src/* $(PKG_BUILD_DIR)/
	mkdir -p $(PKG_BUILD_DIR)/files/etc
	$(CP) ./files/etc/sipserver.conf $(PKG_BUILD_DIR)/files/etc/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-static \
		-I$(PKG_BUILD_DIR) \
		-o $(PKG_BUILD_DIR)/aredn-phonebook \
		$(PKG_BUILD_DIR)/main.c \
		$(PKG_BUILD_DIR)/call-sessions/call_sessions.c \
		$(PKG_BUILD_DIR)/user_manager/user_manager.c \
		$(PKG_BUILD_DIR)/phonebook_fetcher/phonebook_fetcher.c \
		$(PKG_BUILD_DIR)/sip_core/sip_core.c \
		$(PKG_BUILD_DIR)/status_updater/status_updater.c \
		$(PKG_BUILD_DIR)/file_utils/file_utils.c \
		$(PKG_BUILD_DIR)/csv_processor/csv_processor.c \
		$(PKG_BUILD_DIR)/log_manager/log_manager.c \
		$(PKG_BUILD_DIR)/config_loader/config_loader.c \
		$(PKG_BUILD_DIR)/passive_safety/passive_safety.c \
		$(PKG_BUILD_DIR)/software_health/software_health.c \
		$(PKG_BUILD_DIR)/debug_logger/debug_logger.c \
		$(PKG_BUILD_DIR)/mesh_monitor/mesh_monitor.c \
		$(PKG_BUILD_DIR)/mesh_monitor/monitor_config.c \
		$(PKG_BUILD_DIR)/mesh_monitor/routing_adapter.c \
		$(PKG_BUILD_DIR)/mesh_monitor/probe_engine.c \
		$(PKG_BUILD_DIR)/mesh_monitor/agent_discovery.c \
		$(PKG_BUILD_DIR)/mesh_monitor/health_reporter.c \
		$(PKG_BUILD_DIR)/mesh_monitor/http_client.c \
		$(PKG_BUILD_DIR)/mesh_monitor/remote_reporter.c \
		$(PKG_BUILD_DIR)/sip_quality_lib.c \
		$(PKG_BUILD_DIR)/phone_quality_monitor/phone_quality_monitor.c \
		-lpthread
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-static \
		-o $(PKG_BUILD_DIR)/udp-send \
		$(PKG_BUILD_DIR)/udp_send.c
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-static \
		-I$(PKG_BUILD_DIR) \
		-o $(PKG_BUILD_DIR)/sip-quality-test \
		$(PKG_BUILD_DIR)/sip_quality_test.c \
		$(PKG_BUILD_DIR)/sip_quality_lib.c
endef

define Package/aredn-phonebook/preinst
#!/bin/sh
# Remove old CGI files to ensure they get updated
rm -f /www/cgi-bin/loadphonebook
rm -f /www/cgi-bin/showphonebook
rm -f /www/cgi-bin/health
rm -f /www/cgi-bin/crash
rm -f /www/cgi-bin/network
rm -f /www/cgi-bin/version
rm -f /www/cgi-bin/hello
exit 0
endef

define Package/aredn-phonebook/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/aredn-phonebook $(1)/usr/bin/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/udp-send $(1)/usr/bin/sip-options-ping
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/sip-quality-test $(1)/usr/bin/sip-quality-test
	$(INSTALL_BIN) ./files/usr/bin/agent-discovery-status $(1)/usr/bin/
	$(INSTALL_BIN) ./files/usr/bin/test-udp-lan $(1)/usr/bin/
	$(INSTALL_BIN) ./files/usr/bin/test-udp-receive $(1)/usr/bin/
	$(INSTALL_BIN) ./files/usr/bin/test-phone-connectivity $(1)/usr/bin/
	$(INSTALL_DIR) $(1)/etc
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/files/etc/sipserver.conf $(1)/etc/
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/AREDN-Phonebook $(1)/etc/init.d/
	$(INSTALL_DIR) $(1)/www/cgi-bin
	$(INSTALL_BIN) ./files/www/cgi-bin/loadphonebook $(1)/www/cgi-bin/
	$(INSTALL_BIN) ./files/www/cgi-bin/showphonebook $(1)/www/cgi-bin/
	$(INSTALL_BIN) ./files/www/cgi-bin/health $(1)/www/cgi-bin/
	$(INSTALL_BIN) ./files/www/cgi-bin/crash $(1)/www/cgi-bin/
	$(INSTALL_BIN) ./files/www/cgi-bin/network $(1)/www/cgi-bin/
	$(INSTALL_BIN) ./files/www/cgi-bin/version $(1)/www/cgi-bin/
	$(INSTALL_BIN) ./files/www/cgi-bin/hello $(1)/www/cgi-bin/
	$(INSTALL_BIN) ./files/www/cgi-bin/udp-test $(1)/www/cgi-bin/
	$(INSTALL_BIN) ./files/www/cgi-bin/quality $(1)/www/cgi-bin/
endef

$(eval $(call BuildPackage,aredn-phonebook))