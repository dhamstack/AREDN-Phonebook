include $(TOPDIR)/rules.mk

PKG_NAME:=SIPServer
PKG_VERSION:=1.0.0
PKG_RELEASE:=1

PKG_MAINTAINER:=Your Name <your@email.com>
PKG_LICENSE:=GPL-2.0

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

define Package/SIPServer
	SECTION:=net
	CATEGORY:=Network
	SUBMENU:=Telephony
	TITLE:=SIP Server (Version 5)
	URL:=http://example.com
	DEPENDS:=+libpthread
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) -r ./src/. $(PKG_BUILD_DIR)/
	# Ensure the target directory for config files in the staging area exists
	mkdir -p $(PKG_BUILD_DIR)/files/etc
	# Correctly copy the default sipserver.conf from its source location
	$(CP) ./files/etc/sipserver.conf $(PKG_BUILD_DIR)/files/etc/
endef

TARGET_CFLAGS += -I$(PKG_BUILD_DIR) \
	-I$(PKG_BUILD_DIR)/file_utils \
	-I$(PKG_BUILD_DIR)/csv_processor \
	-I$(PKG_BUILD_DIR)/call-sessions \
	-I$(PKG_BUILD_DIR)/user_manager \
	-I$(PKG_BUILD_DIR)/log_manager \
	-I$(PKG_BUILD_DIR)/phonebook_fetcher \
	-I$(PKG_BUILD_DIR)/sip_core \
	-I$(PKG_BUILD_DIR)/status_updater \
	-I$(PKG_BUILD_DIR)/config_loader \
	-I$(PKG_BUILD_DIR)/passive_safety # Add passive safety module

define Build/Compile
	# Compiling individual source files into object files
	$(TARGET_CC) $(TARGET_CFLAGS) -c $(PKG_BUILD_DIR)/main.c -o $(PKG_BUILD_DIR)/main.o
	$(TARGET_CC) $(TARGET_CFLAGS) -c $(PKG_BUILD_DIR)/call-sessions/call_sessions.c -o $(PKG_BUILD_DIR)/call_sessions.o
	$(TARGET_CC) $(TARGET_CFLAGS) -c $(PKG_BUILD_DIR)/user_manager/user_manager.c -o $(PKG_BUILD_DIR)/user_manager.o
	$(TARGET_CC) $(TARGET_CFLAGS) -c $(PKG_BUILD_DIR)/phonebook_fetcher/phonebook_fetcher.c -o $(PKG_BUILD_DIR)/phonebook_fetcher.o
	$(TARGET_CC) $(TARGET_CFLAGS) -c $(PKG_BUILD_DIR)/sip_core/sip_core.c -o $(PKG_BUILD_DIR)/sip_core.o
	$(TARGET_CC) $(TARGET_CFLAGS) -c $(PKG_BUILD_DIR)/status_updater/status_updater.c -o $(PKG_BUILD_DIR)/status_updater.o
	$(TARGET_CC) $(TARGET_CFLAGS) -c $(PKG_BUILD_DIR)/file_utils/file_utils.c -o $(PKG_BUILD_DIR)/file_utils.o
	$(TARGET_CC) $(TARGET_CFLAGS) -c $(PKG_BUILD_DIR)/csv_processor/csv_processor.c -o $(PKG_BUILD_DIR)/csv_processor.o
	$(TARGET_CC) $(TARGET_CFLAGS) -c $(PKG_BUILD_DIR)/log_manager/log_manager.c -o $(PKG_BUILD_DIR)/log_manager.o
	$(TARGET_CC) $(TARGET_CFLAGS) -c $(PKG_BUILD_DIR)/config_loader/config_loader.c -o $(PKG_BUILD_DIR)/config_loader.o
	$(TARGET_CC) $(TARGET_CFLAGS) -c $(PKG_BUILD_DIR)/passive_safety/passive_safety.c -o $(PKG_BUILD_DIR)/passive_safety.o # Add passive safety module
	
	# This is the final "linking" step to create the executable
	$(TARGET_CC) $(TARGET_LDFLAGS) -o $(PKG_BUILD_DIR)/sip-proxy \
		$(PKG_BUILD_DIR)/main.o \
		$(PKG_BUILD_DIR)/call_sessions.o \
		$(PKG_BUILD_DIR)/user_manager.o \
		$(PKG_BUILD_DIR)/phonebook_fetcher.o \
		$(PKG_BUILD_DIR)/sip_core.o \
		$(PKG_BUILD_DIR)/status_updater.o \
		$(PKG_BUILD_DIR)/file_utils.o \
		$(PKG_BUILD_DIR)/csv_processor.o \
		$(PKG_BUILD_DIR)/log_manager.o \
		$(PKG_BUILD_DIR)/config_loader.o \
		$(PKG_BUILD_DIR)/passive_safety.o \
		-lpthread
endef

define Package/SIPServer/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/sip-proxy $(1)/usr/bin/
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/sip-proxy $(1)/etc/init.d/
	
	# Install the sipserver.conf file
	$(INSTALL_DIR) $(1)/etc
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/files/etc/sipserver.conf $(1)/etc/
endef

$(eval $(call BuildPackage,SIPServer))
