#include "game.h"
#include "net_validation.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static void TestEnvelopeValidation(void)
{
	NSpJoinRequestMessage join = {0};
	join.header.what = kNSpJoinRequest;
	join.header.to = kNSpHostID;
	join.header.messageLen = sizeof(join);
	assert(NetValidateInboundEnvelope(kNetInbound_Host, &join.header));

	join.header.messageLen = sizeof(NSpMessageHeader);
	assert(!NetValidateInboundEnvelope(kNetInbound_Host, &join.header));

	NSpGameTerminatedMessage terminated = {0};
	terminated.header.what = kNSpGameTerminated;
	terminated.header.to = kNSpAllPlayers;
	terminated.header.messageLen = sizeof(terminated);
	assert(!NetValidateInboundEnvelope(kNetInbound_Host, &terminated.header));
	assert(NetValidateInboundEnvelope(kNetInbound_Client, &terminated.header));

	NetClientControlInfoMessageType control = {0};
	control.h.what = kNetClientControlInfoMessage;
	control.h.to = kNSpAllPlayers;
	control.h.messageLen = sizeof(control);
	assert(!NetValidateInboundEnvelope(kNetInbound_Host, &control.h));

	NSpMessageHeader unknown = {.what = 'nope', .messageLen = sizeof(NSpMessageHeader)};
	assert(!NetValidateInboundEnvelope(kNetInbound_Host, &unknown));
	assert(!NetValidateInboundEnvelope(kNetInbound_Client, &unknown));
}

static NetPlayerCharTypeMessage ValidCharMessage(void)
{
	NetPlayerCharTypeMessage message = {0};
	message.playerNum = 1;
	message.vehicleType = 0;
	message.sex = 0;
	message.skin = 0;
	message.refreshRate = 60;
	message.connectionType = 1;
	return message;
}

static void TestCharacterValidation(void)
{
	NetPlayerCharTypeMessage message = ValidCharMessage();
	assert(NetValidatePlayerCharPayload(&message, 1, 4));

	message.playerNum = 2;
	assert(!NetValidatePlayerCharPayload(&message, 1, 4));
	message = ValidCharMessage();
	message.vehicleType = -1;
	assert(!NetValidatePlayerCharPayload(&message, 1, 4));
	message = ValidCharMessage();
	message.sex = -1;
	assert(!NetValidatePlayerCharPayload(&message, 1, 4));
	message = ValidCharMessage();
	message.skin = NUM_CAVEMAN_SKINS;
	assert(!NetValidatePlayerCharPayload(&message, 1, 4));
	message = ValidCharMessage();
	message.connectionType = 2;
	assert(!NetValidatePlayerCharPayload(&message, 1, 4));
}

static void TestConfigValidation(void)
{
	NetConfigMessage message = {0};
	message.gameMode = GAME_MODE_MULTIPLAYERRACE;
	message.age = 0;
	message.trackNum = 0;
	message.playerNum = 1;
	message.numPlayers = 2;
	message.difficulty = 0;
	message.targetFPS = 60;
	assert(NetValidateConfigPayload(&message));

	message.numPlayers = MAX_LOCAL_PLAYERS + 1;
	assert(!NetValidateConfigPayload(&message));
	message.numPlayers = 2;
	message.playerNum = 2;
	assert(!NetValidateConfigPayload(&message));
	message.playerNum = 1;
	message.trackNum = NUM_TRACKS;
	assert(!NetValidateConfigPayload(&message));
}

static NetClientControlInfoMessageType ValidControlMessage(void)
{
	NetClientControlInfoMessageType message = {0};
	message.playerNum = 1;
	message.controlBits = 1u << kControlBit_Forward;
	message.analogSteering = (OGLVector2D){.x = -1.0f, .y = 1.0f};
	return message;
}

static void TestControlValidation(void)
{
	NetClientControlInfoMessageType message = ValidControlMessage();
	assert(NetValidateClientControlPayload(&message, 1, 4));

	message.playerNum = 2;
	assert(!NetValidateClientControlPayload(&message, 1, 4));
	message = ValidControlMessage();
	message.pauseState = 2;
	assert(!NetValidateClientControlPayload(&message, 1, 4));
	message = ValidControlMessage();
	message.controlBits = 1u << NUM_CONTROL_BITS;
	assert(!NetValidateClientControlPayload(&message, 1, 4));
	message = ValidControlMessage();
	message.analogSteering.x = NAN;
	assert(!NetValidateClientControlPayload(&message, 1, 4));
	message = ValidControlMessage();
	message.analogSteering.y = 1.01f;
	assert(!NetValidateClientControlPayload(&message, 1, 4));
}

static void TestCursorClamping(void)
{
	char buffer[8] = {0};
	char* cursor = buffer;
	size_t remaining = sizeof(buffer);

	AdvanceTextCursor(3, &cursor, &remaining);
	assert(cursor == buffer + 3 && remaining == 5);
	AdvanceTextCursor(100, &cursor, &remaining);
	assert(cursor == buffer + 7 && remaining == 1);
	AdvanceTextCursor(100, &cursor, &remaining);
	assert(cursor == buffer + 7 && remaining == 1);
	remaining = 0;
	AdvanceTextCursor(1, &cursor, &remaining);
	assert(cursor == buffer + 7 && remaining == 0);
}

static int FormatForTest(const char* text, char* buf, size_t bufSize, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	int result = VFormatTextWithPlaceholder(text, buf, bufSize, format, args);
	va_end(args);
	return result;
}

static void TestPlaceholderFormatting(void)
{
	char exact[8];
	assert(FormatForTest("A#Z", exact, sizeof(exact), "%s", "12345") == 7);
	assert(strcmp(exact, "A12345Z") == 0);

	char shortBuffer[5];
	assert(FormatForTest("A#Z", shortBuffer, sizeof(shortBuffer), "%s", "12345") == 4);
	assert(strcmp(shortBuffer, "A123") == 0);

	char oneByte[1];
	assert(FormatForTest("A#Z", oneByte, sizeof(oneByte), "%s", "12345") == 0);
	assert(oneByte[0] == '\0');

	assert(FormatForTest("plain", NULL, 0, "%d", 1) == 0);
}

int main(void)
{
	TestEnvelopeValidation();
	TestCharacterValidation();
	TestConfigValidation();
	TestControlValidation();
	TestCursorClamping();
	TestPlaceholderFormatting();
	puts("Validation tests passed.");
	return 0;
}
