"""
INSTALL
-------
pip install websockets
pip install faster-whisper
pip install requests
pip install numpy
pip install pydub
pip install audioop-lts

RUN
---
Pocket_Assistant_Server_V1.0.py

ESP32 CONNECTS TO
-----------------
ws://YOUR_PC_IP:9000

==================================================
"""
import asyncio
import websockets
import tempfile
import os
import json
import wave
import uuid
import requests
import time
import re
from datetime import datetime
from faster_whisper import WhisperModel
from pydub import AudioSegment
from io import BytesIO

# ==================================================
# CLIENT CONFIG CLASS
# ==================================================

class ClientConfig:

    def __init__(self,data):

        self.voice_name = data.get(
            "VOICE_NAME",
            "af_heart"
        )

        self.assistant_name = data.get(
            "ASSISTANT_NAME",
            "AI"
        )

        self.user_name = data.get(
            "USER_NAME",
            "User"
        )

        self.personality = data.get(
            "ASSISTANT_PERSONALITY",
            ""
        )

        self.lore = data.get(
            "ASSISTANT_LORE",
            ""
        )

# ==================================================
# SERVER
# ==================================================

HOST = "0.0.0.0"
PORT = 9000
DEBUG = True

# ==================================================
# KOBOLDCPP
# ==================================================

KOBOLD_URL = "http://127.0.0.1:5001/api/extra/generate/stream"

KOBOLD_HEALTH = "http://127.0.0.1:5001/api/v1/model"


# ==================================================
# KOKORO-WEB
# ==================================================


KOKOROWEB_URL = "http://localhost:3000/api/v1/audio/speech"

KOKOROWEB_HEALTH = "http://localhost:3000/health"

VOICE_NAME = "af_heart"

# ==================================================
# WHISPER
# ==================================================

WHISPER_MODEL = "medium"

# ==================================================
# LOAD ASSISTANT CONMFIG
# ==================================================

CONFIG_DIR = "configs"


def load_assistant_config(filename):

    path = os.path.join(CONFIG_DIR, filename)

    if not os.path.exists(path):
        raise Exception(
            f"Config file not found: {path}"
        )


    config = {}

    current_key = None
    buffer = []


    with open(path, "r", encoding="utf-8") as f:

        for line in f:

            line = line.rstrip()


            # Empty line
            if not line:
                if current_key:
                    buffer.append("")
                continue


            # New key
            if "=" in line and not line.startswith(" "):

                if current_key:
                    config[current_key] = "\n".join(buffer).strip()


                current_key, value = line.split("=",1)
                current_key = current_key.strip()
                buffer = []

                if value:
                    buffer.append(value)

            else:

                buffer.append(line)


    if current_key:
        config[current_key] = "\n".join(buffer).strip()


    return config

# ==================================================
# MEMORY
# ==================================================

conversation_history = []

MAX_HISTORY = 80

# ==================================================
# LOGGING
# ==================================================

def log(message):

    now = datetime.now().strftime(
        "%H:%M:%S"
    )

    print(f"[{now}] {message}")

# ==================================================
# LOAD WHISPER
# ==================================================

log("Loading Whisper model...")
model = WhisperModel(WHISPER_MODEL,device="cpu",compute_type="int8")
log("Whisper loaded")

# ==================================================
# CLEAN AI RESPONSE
# ==================================================

def clean_ai_response(text):


    # Remove <think> blocks
    text = re.sub(r"<think>.*?</think>", "",text,flags=re.DOTALL )

    # Remove leftover tags
    text = re.sub(

        r"</?think>",

        "",

        text
    )

    # Remove extra whitespace
    text = re.sub(

        r"\n\s*\n",

        "\n",

        text
    )

    # Remove markdown
    text = text.replace("*", "")

    # Remove roleplay
    text = re.sub(
        r"\(.*?\)",
        "",
        text
    )

    return text.strip()

# ==================================================
# VALIDATE AI RESPONSE
# ==================================================

def is_valid_response(text):

    if not text:
        return False

    # Empty
    if len(text.strip()) == 0:
        return False

    # Only think tags
    cleaned = clean_ai_response(text).strip()

    if len(cleaned) == 0:
        return False

    # Garbage tokens
    invalid = [

        "<think>",
        "</think>",
        "null",
        "None"
    ]

    if cleaned in invalid:
        return False

    return True

# ==================================================
# CONVERT AUDIO FOR ESP32
# ==================================================

def convert_audio_for_esp32(audio_bytes):

    log("Converting audio...")
    log("")
    # Load WAV
    audio = AudioSegment.from_file(BytesIO(audio_bytes),format="wav")

    # Convert format
    audio = audio.set_frame_rate(16000)
    audio = audio.set_channels(1)
    audio = audio.set_sample_width(2)

    # Export RAW PCM
    raw_audio = audio.raw_data

    log(
        f"Converted audio: "
        f"{len(raw_audio)} bytes"
    )
    log("")
    return raw_audio

# ==================================================
# HEALTH CHECKS
# ==================================================

def check_kobold():

    try:

        response = requests.get(

            KOBOLD_HEALTH,
            timeout=5
        )

        if response.status_code == 200:

            data = response.json()

            log(
                "[OK] KoboldCpp ready"
            )
            log("")
            log(
                f"Model: "
                f"{data.get('result')}"
            )
            log("")
            return True

        else:

            log(
                f"[ERROR] KoboldCpp status: "
                f"{response.status_code}"
            )

    except Exception as e:

        log(
            "[ERROR] KoboldCpp offline"
        )

        log(str(e))

    return False

def check_KokoroWeb():

    try:

        payload = {
            "model": "model",
            "voice": "af_heart",
            "input": "ready",
            "response_format": "wav"
        }

        response = requests.post(
            KOKOROWEB_URL,
            json=payload,
            timeout=20
        )


        if response.status_code == 200:

            if len(response.content) > 1000:

                log(
                    "[OK] KokoroWeb TTS ready"
                )
                log("")
                return True


        log(
            f"[ERROR] KokoroWeb returned "
            f"{response.status_code}"
        )

        log(response.text)


    except requests.exceptions.ConnectionError:

        log(
            "[ERROR] KokoroWeb offline"
        )


    except Exception as e:

        log(
            "[ERROR] KokoroWeb check failed"
        )

        log(str(e))


    return False

def wait_for_services():

    log("Checking services...")
    log("")
    while True:

        kobold_ok = check_kobold()
        kokoro_ok = check_KokoroWeb()

        if kobold_ok and kokoro_ok:

            log("All services ready")
            log("")
            return True

        log("Retrying in 5 seconds...")
        log("")
        time.sleep(5)

# ==================================================
# MEMORY
# ==================================================

def save_history(role, content):
    global conversation_history
    conversation_history.append({"role": role, "content": content})

    if len(conversation_history) > MAX_HISTORY:
        conversation_history = conversation_history[-MAX_HISTORY:]

# ==================================================
# PROMPT
# ==================================================

def build_prompt(user_text, config):

    history_lines = []

    for msg in conversation_history[-MAX_HISTORY:]:
        role = "user" if msg['role'] == config.user_name else "assistant"
        history_lines.append(f"<|im_start|>{role}\n{msg['content']}<|im_end|>\n")

    history = "".join(history_lines)

    prompt = (
    f"<|im_start|>system\n"
    f"{config.personality}\n\n"
    f"{config.lore}"
    f"<|im_end|>\n"
    f"{history}"
    f"<|im_start|>user\n{user_text}<|im_end|>\n"
    f"<|im_start|>assistant\n"
)

    return prompt

# ==================================================
# SAVE WAV
# ==================================================

def save_wav(raw_audio,output_path):

    with wave.open(output_path,"wb") as wf:

        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(16000)
        wf.writeframes(raw_audio)

# ==================================================
# TRANSCRIBE
# ==================================================

def transcribe_audio(wav_path):

    log("Transcribing...")
    log("")
    start = time.time()

    segments, info = model.transcribe(

        wav_path,

        beam_size=5,

        vad_filter=True
    )

    text = ""

    for seg in segments:

        text += seg.text

    elapsed = round(
        time.time() - start,
        2
    )

    log(
        f"Transcription took "
        f"{elapsed}s"
    )
    log("")
    return text.strip()

# ==================================================
# STREAM KOBOLDCPP
# ==================================================

async def stream_kobold(prompt):

    payload = {

        "prompt": prompt,
        "max_context_length": 4096,
        "max_length": 512,
        "temperature": 0.8,
        "top_p": 0.9,
        "top_k": 30,
        "top_a": 0,
        "rep_pen": 1.05,
        "stop_sequence": ["\nUser:", "<|im_end|>", "\nAI:"],
        "stream": True,
        "trim_stop": True,
        "reasoning_effort": "none"
    }

    log(
        "Sending to KoboldCpp..."
    )
    log("")

    response = requests.post(

        KOBOLD_URL,

        json=payload,

        stream=True
    )

    log(
        f"Kobold status: "
        f"{response.status_code}"
    )
    log("")

    # STREAM TOKENS
    for line in response.iter_lines():

        if not line:

            continue

        decoded = line.decode(
            "utf-8"
        )

        # SSE format
        if not decoded.startswith(
            "data:"
        ):

            continue

        # Remove "data:"
        chunk = decoded[5:].strip()

        # End marker
        if chunk == "[DONE]":

            break

        # Parse JSON
        try:

            data = json.loads(
                chunk
            )

        except Exception as e:

            log(
                f"JSON parse error: {e}"
            )

            continue

        # Extract token
        token = data.get("token","")

        # Skip empty
        if not token:

            continue

        # Debug print
        if DEBUG:

            print(
                "",#token
                end="",
                flush=True
            )

        # Yield ONLY TOKEN
        yield token

    print()

# ==================================================
# KokoroWeb TTS
# ==================================================

def generate_tts(text, config):

    log("Generating TTS...")
    log("")
    payload = {
        "model": "model",
        "voice": config.voice_name,
        "input": text,
        "response_format": "wav"
    }
    log("------------------------------------------------------------------------------------------------------")
    log("TTS Payload:")
    print(json.dumps(payload, indent=2))
    log("------------------------------------------------------------------------------------------------------")

    try:
        response = requests.post(
            KOKOROWEB_URL,
            json=payload,
            timeout=120
        )

    except Exception as e:
        raise Exception(
            f"Kokoro Web connection failed: {e}"
        )


    log(
        f"Kokoro Web status: "
        f"{response.status_code}"
    )
    log("")

    # HTTP error handling
    if response.status_code != 200:
        try:
            print(
                json.dumps(
                    response.json(),
                    indent=2
                )
            )
        except:
            print(response.text)

        raise Exception(
            f"Kokoro Web TTS failed: "
            f"{response.status_code}"
        )



    content_type = response.headers.get("content-type", "")


    log(
        f"Content-Type: {content_type}"
    )
    log("")

    # Direct audio response
    if ("audio" in content_type
       or 
        "wav" in content_type
    ):

        log(
            f"Received audio: "
            f"{len(response.content)} bytes"
        )
        log("")
        return response.content


    # JSON response fallback
    try:

        data = response.json()

        print(
            json.dumps(
                data,
                indent=2
            )
        )

    except Exception:

        raise Exception(
            "Kokoro returned unknown response:\n"
            + response.text
        )


    audio_url = (
        data.get("audio_url")
        or
        data.get("url")
        or
        data.get("output_file_url")
    )


    if not audio_url:

        raise Exception(
            "No audio returned from Kokoro Web.\n"
            f"Response: {data}"
        )


    # Download returned file
    if audio_url.startswith("/"):

        audio_url = ("http://127.0.0.1:3000" + audio_url)


    log("Downloading audio:")
    log("")
    log(audio_url)
    log("")


    audio_response = requests.get(
        audio_url,
        timeout=120
    )


    log(
        f"Downloaded "
        f"{len(audio_response.content)} bytes"
    )
    log("")


    return audio_response.content

# ==================================================
# STREAM AUDIO
# ==================================================

async def stream_audio(websocket, audio_bytes):

    CHUNK = 2048
    BYTES_PER_SECOND = 32000

    log(f"Streaming {len(audio_bytes)} bytes")
    log("")

    for i in range(0, len(audio_bytes), CHUNK):

        chunk = audio_bytes[i:i+CHUNK]
        await websocket.send(chunk)
        await asyncio.sleep((len(chunk) / BYTES_PER_SECOND) * 0.75)

    await websocket.send("AUDIO_END")

    log( "Audio complete")
    log("")

# ==================================================
# CLIENT HANDLER
# ==================================================

async def handle_client(websocket):

    log("ESP32 connected")
    log("")


    # Wait for client configuration
    config_file = await websocket.recv()

    if not isinstance(config_file,str):
        await websocket.close()
        return

    conversation_history.clear()
    log("Conversation history cleared")
    log("")

    log(f"Loading assistant config: {config_file}")

    try:
        data = load_assistant_config(config_file)
        client_config = ClientConfig(data)


    except Exception as e:
        log(str(e))

        await websocket.send("CONFIG_ERROR")
        return

    audio_buffer = bytearray()

    try:
        async for message in websocket:
        
            # AUDIO
            if isinstance(message,bytes):
                audio_buffer.extend(message)

            # END OF SPEECH
            elif message == "END":

                log("Speech ended")
                log("")

                # SAVE TEMP WAV          
                temp_path = os.path.join(tempfile.gettempdir(),str(uuid.uuid4())+ ".wav")

                save_wav(audio_buffer,temp_path)

                # WHISPER
                user_text = transcribe_audio(temp_path)

                if len(user_text.strip()) == 0:

                    log("Empty speech")
                    log("")

                    # Tell ESP32 to resume listening
                    await websocket.send("NO_SPEECH")

                    audio_buffer = bytearray()

                    continue
                else:
                    await websocket.send(user_text)
                log("------------------------------------------------------------------------------------------------------")
                log(
                    f"{client_config.user_name}: "
                    f"{user_text}"
                )
                log("------------------------------------------------------------------------------------------------------")

                # PROMPT
                prompt = build_prompt(user_text,client_config)

                log("------------------------------------------------------------------------------------------------------")
                log("The Prompt: \n" + prompt)
                log("------------------------------------------------------------------------------------------------------")

                # KOBOLD
                ai_response = ""

                async for token in stream_kobold(prompt):
                    ai_response += token

                ai_response = clean_ai_response( ai_response.strip())
             
                # Validate response              
                if not is_valid_response(ai_response):
                    ai_response = "Can you repeat that?"
                else:
                    # SAVE MEMORY IF VALID
                    save_history(client_config.user_name,user_text)            
                    save_history(client_config.assistant_name,ai_response)
                log("------------------------------------------------------------------------------------------------------")
                log(
                    f"AI Response: "
                    f"{ai_response}"
                )
                log("------------------------------------------------------------------------------------------------------")
                                
                # TTS             
                tts_audio = generate_tts(ai_response,client_config)
              
                # convert audio for esp32
                tts_audio = convert_audio_for_esp32(tts_audio)

                # STREAM AUDIO
                await websocket.send("AUDIO_START")
                await stream_audio(websocket,tts_audio)

                # CLEANUP
                try:
                    os.remove(temp_path)

                except:
                    pass

                audio_buffer = bytearray()
                log("Request complete")
                log("")

    except Exception as e:

        log( "Client disconnected")
        log(str(e))
        log("")

# ==================================================
# MAIN
# ==================================================

async def main():

    wait_for_services()

    log(
        f"Starting server on "
        f"ws://{HOST}:{PORT}"
    )
    log("")

    async with websockets.serve(

        handle_client,

        HOST,

        PORT,

        max_size=20_000_000,

        ping_interval=None,

        compression=None
    ):

        log("Server ready")
        log("")
        await asyncio.Future()

asyncio.run(main())

