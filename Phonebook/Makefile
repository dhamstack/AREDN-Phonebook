include $(TOPDIR)/rules.mk

PKG_NAME:=aredn-phonebook
PKG_VERSION:=1.4.5
PKG_RELEASE:=1

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

define Package/aredn-phonebook
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=Telephony
  TITLE:=AREDN Phonebook
  URL:=https://github.com/dhamstack/AREDN-Phonebook
  DEPENDS:=+libpthread
endef

define Package/aredn-phonebook/description
  AREDN Phonebook SIP Server with UAC load testing support.
  Includes passive safety features for Amateur Radio Emergency Data Networks.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
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
		$(PKG_BUILD_DIR)/uac/uac.c \
		$(PKG_BUILD_DIR)/uac/uac_sip_builder.c \
		$(PKG_BUILD_DIR)/uac/uac_sip_parser.c \
		-lpthread
endef

define Package/aredn-phonebook/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/aredn-phonebook $(1)/usr/bin/
	$(INSTALL_DIR) $(1)/etc
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/files/etc/sipserver.conf $(1)/etc/
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/AREDN-Phonebook $(1)/etc/init.d/
	$(INSTALL_DIR) $(1)/www/cgi-bin
	$(INSTALL_BIN) ./files/www/cgi-bin/loadphonebook $(1)/www/cgi-bin/
	$(INSTALL_BIN) ./files/www/cgi-bin/showphonebook $(1)/www/cgi-bin/
endef

$(eval $(call BuildPackage,aredn-phonebook))