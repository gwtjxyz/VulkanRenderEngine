module;

export module input;

export struct ButtonState {
    enum class State {
        Pressed,
        Released,
    };

    ButtonState::State currentState = State::Released;
    ButtonState::State previousState = State::Released;

    bool isHeldDown() const {
        return currentState == State::Pressed;
    }

    bool wasJustPressed() const {
        return currentState == State::Pressed && previousState == State::Released;
    }

    void press() {
        if (currentState == State::Released) {
            currentState = State::Pressed;
        } else if (previousState == State::Released) {
            previousState = State::Pressed;
        }
    }

    // I don't like this; have to keep polling events for every key we use in the application
    // then again, maybe this is okay? How do games typically handle this?
    // Maybe I need a full-on event system that enqueues events on top of GLFW?
    void release() {
        if (currentState == State::Pressed) {
            currentState = State::Released;
        } else if (previousState == State::Pressed) {
            previousState = State::Released;
        }
    }
};

export struct InputState {
    // TODO
};
