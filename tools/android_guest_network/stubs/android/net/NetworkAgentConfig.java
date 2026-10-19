package android.net;

/** Compile-only signature verified against the selected guest; never packaged. */
public final class NetworkAgentConfig {
  public static final class Builder {
    public Builder() {}

    public Builder setLegacyType(int value) {
      return this;
    }

    public Builder setLegacyTypeName(String value) {
      return this;
    }

    public Builder setLegacyExtraInfo(String value) {
      return this;
    }

    public Builder setExplicitlySelected(boolean value) {
      return this;
    }

    public NetworkAgentConfig build() {
      throw new UnsupportedOperationException();
    }
  }
}
