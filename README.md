# Pocket-AI-Assistant
Client and server architecture client running on a esp32 s3-n16r8 acts as a interface for the user using mic screen and speaker to let the user interact with a LLM. The user uses natural speech and receives responses in speech from the AI, STT using Whisper, LLM using koboldcpp and Kokoro Web for TTS. The server runs using python and loads a configurasion file requested by the client.

V2 adds the ability to generate images and display them on the build in screen it also adds the ability of picking the config settings to load and lets the user review the TTS before processing from the LLM. The config files now let you customizes the assistant more deeply.
