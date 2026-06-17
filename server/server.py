#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Smart Curtain - Python Bridge Server
Связывает Android приложение и ESP32 через HTTP/JSON
"""

from flask import Flask, request, jsonify
from flask_cors import CORS
import requests
import json
import time
import logging
from threading import Thread

# Настройка логирования
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

app = Flask(__name__)
CORS(app)  # Разрешаем запросы с Android

# ============= КОНФИГУРАЦИЯ =============
# IP адрес ESP32 (можно изменить через API)
ESP32_CONFIG = {
    "ip": "192.168.1.100",      # По умолчанию
    "port": 80,
    "timeout": 3
}

# Хранилище состояния штор
CURTAINS = {
    "curtain_1": {
        "id": "curtain_1",
        "name": "Умные шторы",
        "position": 0,
        "is_moving": False,
        "status": "unknown",
        "last_update": 0,
        "wifi_rssi": 0
    }
}

# ============= ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ =============

def get_esp32_url(endpoint=""):
    """Формирует URL для запроса к ESP32"""
    return f"http://{ESP32_CONFIG['ip']}:{ESP32_CONFIG['port']}/{endpoint}"

def send_to_esp32(endpoint):
    """Отправляет GET запрос на ESP32"""
    try:
        url = get_esp32_url(endpoint)
        logger.info(f"→ ESP32: GET {url}")
        
        response = requests.get(
            url,
            timeout=ESP32_CONFIG['timeout']
        )
        
        if response.status_code == 200:
            logger.info(f"← ESP32: {response.text[:100]}")
            return True, response.text
        else:
            logger.error(f"ESP32 error: {response.status_code}")
            return False, f"HTTP {response.status_code}"
            
    except requests.exceptions.Timeout:
        logger.error("ESP32 timeout")
        return False, "Timeout"
    except requests.exceptions.ConnectionError:
        logger.error("ESP32 connection failed")
        return False, "Connection failed"
    except Exception as e:
        logger.error(f"ESP32 error: {str(e)}")
        return False, str(e)

def update_curtain_state(curtain_id):
    """Обновляет состояние шторы из ESP32"""
    success, response = send_to_esp32("status")
    
    if success and response:
        # Парсим ответ ESP32 (JSON или текст)
        try:
            # Пробуем парсить как JSON
            import json
            data = json.loads(response)
            CURTAINS[curtain_id].update({
                "position": data.get("position", 0),
                "is_moving": data.get("is_moving", False),
                "status": data.get("status", "unknown"),
                "last_update": int(time.time())
            })
        except:
            # Если не JSON - парсим текст
            resp_lower = response.lower()
            if "opened" in resp_lower:
                CURTAINS[curtain_id]["status"] = "opened"
                CURTAINS[curtain_id]["position"] = 100
            elif "closed" in resp_lower:
                CURTAINS[curtain_id]["status"] = "closed"
                CURTAINS[curtain_id]["position"] = 0
            elif "moving" in resp_lower:
                CURTAINS[curtain_id]["status"] = "moving"
                CURTAINS[curtain_id]["is_moving"] = True
            
            CURTAINS[curtain_id]["last_update"] = int(time.time())
    
    return CURTAINS[curtain_id]

# ============= API ЭНДПОИНТЫ =============

@app.route('/api/ping', methods=['GET'])
def api_ping():
    """Проверка работы сервера"""
    return jsonify({
        "success": True,
        "message": "pong",
        "timestamp": int(time.time())
    })

@app.route('/api/curtains', methods=['GET'])
def api_get_curtains():
    """Список всех штор"""
    return jsonify({
        "curtains": list(CURTAINS.values())
    })

@app.route('/api/curtain/state/<curtain_id>', methods=['GET'])
def api_get_curtain_state(curtain_id):
    """Состояние конкретной шторы"""
    if curtain_id not in CURTAINS:
        return jsonify({"error": "Curtain not found"}), 404
    
    # Обновляем состояние из ESP32
    state = update_curtain_state(curtain_id)
    
    return jsonify({
        "curtain_state": state
    })

@app.route('/api/curtain/command', methods=['POST'])
def api_send_command():
    """Отправка команды на ESP32"""
    data = request.json
    curtain_id = data.get('curtain_id', 'curtain_1')
    action = data.get('action')
    position = data.get('position', -1)
    
    logger.info(f"Command: {action} for {curtain_id}" + (f" at {position}%" if position >= 0 else ""))
    
    if curtain_id not in CURTAINS:
        return jsonify({
            "success": False,
            "message": "Curtain not found"
        }), 404
    
    # Преобразуем команду в эндпоинт ESP32
    esp32_endpoint = "stop"  # по умолчанию
    
    if action == "open":
        esp32_endpoint = "open"
    elif action == "close":
        esp32_endpoint = "close"
    elif action == "stop":
        esp32_endpoint = "stop"
    elif action == "set_position" and position >= 0:
        esp32_endpoint = f"set?pos={position}"
    
    # Отправляем на ESP32
    success, response = send_to_esp32(esp32_endpoint)
    
    if success:
        # Обновляем состояние
        update_curtain_state(curtain_id)
        
        return jsonify({
            "success": True,
            "message": f"Command '{action}' executed",
            "curtain_state": CURTAINS[curtain_id]
        })
    else:
        return jsonify({
            "success": False,
            "message": response
        }), 500

@app.route('/api/nodemcu/status', methods=['POST'])
def api_nodemcu_status():
    """ESP32 отправляет свой статус (webhook)"""
    data = request.json
    curtain_id = data.get('curtain_id', 'curtain_1')
    
    if curtain_id not in CURTAINS:
        CURTAINS[curtain_id] = {
            "id": curtain_id,
            "name": f"Штора {curtain_id}",
            "position": 0,
            "is_moving": False,
            "status": "unknown",
            "last_update": 0
        }
    
    CURTAINS[curtain_id].update({
        "position": data.get('position', 0),
        "is_moving": data.get('is_moving', False),
        "status": data.get('status', 'unknown'),
        "last_update": int(time.time())
    })
    
    return jsonify({"success": True})

@app.route('/api/config/esp32', methods=['GET', 'POST'])
def api_esp32_config():
    """Получить или изменить конфигурацию ESP32"""
    global ESP32_CONFIG
    
    if request.method == 'POST':
        data = request.json
        if 'ip' in data:
            ESP32_CONFIG['ip'] = data['ip']
        if 'port' in data:
            ESP32_CONFIG['port'] = data['port']
        logger.info(f"ESP32 config updated: {ESP32_CONFIG['ip']}:{ESP32_CONFIG['port']}")
        return jsonify({"success": True, "config": ESP32_CONFIG})
    
    return jsonify(ESP32_CONFIG)

@app.route('/api/health', methods=['GET'])
def api_health():
    """Полная проверка системы"""
    # Проверяем ESP32
    esp32_ok, _ = send_to_esp32("ping")
    
    return jsonify({
        "server": "ok",
        "esp32": "ok" if esp32_ok else "offline",
        "timestamp": int(time.time())
    })

# ============= ЗАПУСК =============

if __name__ == '__main__':
    print("\n" + "="*50)
    print("   SMART CURTAIN - Python Bridge Server")
    print("="*50)
    print(f"   ESP32 target: {ESP32_CONFIG['ip']}:{ESP32_CONFIG['port']}")
    print("   Server running on: http://0.0.0.0:5000")
    print("="*50 + "\n")
    
    app.run(
        host='0.0.0.0',
        port=5000,
        debug=True,
        threaded=True
    )