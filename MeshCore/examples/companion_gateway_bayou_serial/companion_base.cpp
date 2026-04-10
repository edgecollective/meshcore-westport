#ifdef LORA_FREQ
#undef LORA_FREQ
#endif
#define LORA_FREQ 910.525

#ifdef LORA_BW
#undef LORA_BW
#endif
#define LORA_BW 62.5

#ifdef LORA_SF
#undef LORA_SF
#endif
#define LORA_SF 7

#ifdef LORA_CR
#undef LORA_CR
#endif
#define LORA_CR 5

#include "../companion_radio/DataStore.cpp"
#include "../companion_radio/MyMesh.cpp"
#include "../companion_gateway_serial/GatewayMesh.cpp"
