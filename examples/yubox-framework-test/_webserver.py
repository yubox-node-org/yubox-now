from flask import Blueprint, Response, jsonify
from time import sleep
from datetime import datetime
from random import randint
import json

yubox_blueprint = Blueprint("yubox_blueprint", __name__)

@yubox_blueprint.route('/yubox-api/lectura/events')
def event():
    def eventStream():
        while True:
            sleep(1)            
            data = {"ts": int(datetime.now().timestamp()), 
                    "pressure": randint(0,100), 
                    "temperature" : randint(0,100)}

            #print(str(data))
            yield f"data: {json.dumps(data)}\n\n"

    return Response(eventStream(), mimetype="text/event-stream")


