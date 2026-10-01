get_filename_component(_transport_root "../.." ABSOLUTE BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(_runtime_sources EndpointDirectory V1Address V1Configuration NativeOwnerTable
    ServiceRecordTable ServiceRecordBinding SendQueue InputChannels ApplicationAttachments
    RuntimeSendPath DSoftBusSenderResource NativeSendExecution ProcessReceivePath ProcessCoordinator
    DSoftBusTransport DSoftBusTransportDescriptor HostProviderLoop ProviderAdapter
    PublicSocketBackend ExperimentalRuntime)
set(_files)
foreach(_source IN LISTS _runtime_sources)
    list(APPEND _files "${_transport_root}/src/${_source}.cpp")
endforeach()
