include $(TOPDIR)/rules.mk

PKG_NAME:=sipserver
PKG_VERSION:=1.1.7
PKG_RELEASE:=1

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

define Package/sipserver
	SECTION:=net
	CATEGORY:=Network
	SUBMENU:=Telephony
	TITLE:=AREDN SIP Server
	URL:=https://github.com/dhamstack/AREDN-Phonebook
	DEPENDS:=+libpthread
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

TARGET_CFLAGS += -std=gnu99 -Wall -Wextra

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-I$(PKG_BUILD_DIR) \
		-o $(PKG_BUILD_DIR)/sipserver \
		$(PKG_BUILD_DIR)/*.c \
		$(PKG_BUILD_DIR)/*/*.c \
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