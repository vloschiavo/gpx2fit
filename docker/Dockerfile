# syntax=docker/dockerfile:1

# Build the WebAssembly bundle and stamp the web version metadata.
FROM emscripten/emsdk:4.0.10 AS builder

RUN apt-get update \
    && apt-get install -y --no-install-recommends xxd \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# Build wasm bundle and stamp build metadata for cache-busting/update checks.
# WEB_BUILD_ID can be supplied at build time for reproducible/stamped images.
ARG WEB_BUILD_ID
ARG WEB_SHA=unknown
ARG WEB_BUILD_TIME=unknown
RUN make wasm \
    && if [ -n "$WEB_BUILD_ID" ]; then \
         make web-version WEB_BUILD_ID="$WEB_BUILD_ID" WEB_SHA="$WEB_SHA" WEB_BUILD_TIME="$WEB_BUILD_TIME"; \
       else \
         make web-version WEB_SHA="$WEB_SHA" WEB_BUILD_TIME="$WEB_BUILD_TIME"; \
       fi

# Minimal static runtime.
FROM httpd:alpine AS runtime

# Enable modules needed by docker/httpd-gpx2fit.conf. mod_log_config and
# mod_unixd are enabled in the stock httpd.conf.
RUN sed -i \
      -e 's/^#LoadModule remoteip_module/LoadModule remoteip_module/' \
      -e 's/^#LoadModule headers_module/LoadModule headers_module/' \
      /usr/local/apache2/conf/httpd.conf \
    && printf '\nInclude conf/extra/gpx2fit.conf\n' >> /usr/local/apache2/conf/httpd.conf

COPY docker/httpd-gpx2fit.conf /usr/local/apache2/conf/extra/gpx2fit.conf

# Copy only the static web bundle (no source, no build tools).
RUN rm -rf /usr/local/apache2/htdocs/*
COPY --from=builder /src/web/ /usr/local/apache2/htdocs/

EXPOSE 80
