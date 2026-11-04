// Synthetic export-contract fixture; never a Fast DDS factory or transport.
#ifndef PROFILE_FIXTURE_NO_ABI
extern "C" __declspec(dllexport) unsigned FastddsDsoftbusFactoryHookAbi()
{
#ifdef PROFILE_FIXTURE_BAD_ABI
    return 21407;
#else
    return 21405;
#endif
}
#endif
#ifndef PROFILE_FIXTURE_NO_SECURITY
extern "C" __declspec(dllexport) unsigned FastddsDsoftbusFactorySecurityMode()
{
#ifdef PROFILE_FIXTURE_BAD_SECURITY
    return 0;
#else
    return 1;
#endif
}
#endif
