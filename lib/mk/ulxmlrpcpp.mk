ULXMLRPCPP_PORT_DIR = $(call select_from_ports,ulxmlrpcpp)
ULXMLRPCPP_DIR = $(ULXMLRPCPP_PORT_DIR)/src/lib/ulxmlrpcpp

SRC_CC = ulxmlrpcpp.cpp \
	ulxr_http_client.cpp \
	ulxr_signature.cpp \
	ulxr_cached_resource.cpp \
	ulxr_http_protocol.cpp \
	ulxr_ssl_connection.cpp \
	ulxr_call.cpp \
	ulxr_http_server.cpp \
	ulxr_tcpip_connection.cpp \
	ulxr_callparse_base.cpp \
	ulxr_value.cpp \
	ulxr_callparse.cpp \
	ulxr_mtrpc_server.cpp \
	ulxr_valueparse_base.cpp \
	ulxr_callparse_wb.cpp \
	ulxr_mutex.cpp \
	ulxr_valueparse.cpp \
	ulxr_connection.cpp \
	ulxr_protocol.cpp \
	ulxr_valueparse_wb.cpp \
	ulxr_dispatcher.cpp \
	ulxr_requester.cpp \
	ulxr_virtual_resource.cpp \
	ulxr_except.cpp \
	ulxr_response.cpp \
	ulxr_wbxmlparse.cpp \
	ulxr_expatwrap.cpp \
	ulxr_responseparse_base.cpp \
	ulxr_xmlparse_base.cpp \
	ulxr_file_resource.cpp \
	ulxr_responseparse.cpp \
	ulxr_xmlparse.cpp \
	ulxr_htmlform_handler.cpp \
	ulxr_responseparse_wb.cpp \
	ulxr_log4j.cpp

vpath %.cpp $(ULXMLRPCPP_DIR)/ulxmlrpcpp

INC_DIR += $(ULXMLRPCPP_DIR) \
           $(REP_DIR)/include/ulxmlrpcpp

LIBS += libc pthread stdcxx expat libssl

CC_OPT += -D__unix__
CC_OPT += -DHAVE_CONFIG_H
CC_OPT += -DULXR_HAVE_CONFIG_H
SHARED_LIB = yes
