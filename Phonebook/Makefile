include $(TOPDIR)/rules.mk

PKG_NAME:=sipserver
PKG_VERSION:=1.3.1
PKG_RELEASE:=1

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

define Package/sipserver
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=Telephony
  TITLE:=AREDN SIP Server
  URL:=https://github.com/dhamstack/AREDN-Phonebook
endef

define Package/sipserver/description
  AREDN Phonebook SIP Server with passive safety features
  for Amateur Radio Emergency Data Networks.
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
		-o $(PKG_BUILD_DIR)/sipserver \
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
		-lpthread
endef

define Package/sipserver/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/sipserver $(1)/usr/bin/
	$(INSTALL_DIR) $(1)/etc
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/files/etc/sipserver.conf $(1)/etc/
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/sip-proxy $(1)/etc/init.d/
endef

$(eval $(call BuildPackage,sipserver))