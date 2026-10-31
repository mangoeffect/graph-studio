/*! coi-serviceworker v0.1.7 - Guido Zuidhof and contributors, licensed under MIT
 *  + GraphStudio 扩展（cache-first，见下方 GS_ 标记段）：核心产物
 *    （wasm/glue/worker/qtloader）回放缓存，版本键由 package_web.py 打包期
 *    烧入（"gs-web-dev" → "gs-web-<version>"）；存原始响应、回放时补
 *    COOP/COEP（避免 coep 降级后缓存头陈旧）。 */
let coepCredentialless = false;
if (typeof window === 'undefined') {
    // GS_: 核心产物 cache-first。GitHub Pages 一切资产 max-age=600，10 分钟
    // 后回访要重新过线 11MB wasm——这里从 Cache Storage 直接回放。
    const GS_APP_CACHE = "gs-web-dev";
    const gsCacheable = (url) => url.origin === self.location.origin
        && (/\.(wasm)$/.test(url.pathname)
            || /\/(graph_studio\.js|graph_studio\.worker\.js|qtloader\.js)$/
                .test(url.pathname));
    const gsRewrite = (response) => {
        const newHeaders = new Headers(response.headers);
        newHeaders.set("Cross-Origin-Embedder-Policy",
            coepCredentialless ? "credentialless" : "require-corp");
        if (!coepCredentialless) {
            newHeaders.set("Cross-Origin-Resource-Policy", "cross-origin");
        }
        newHeaders.set("Cross-Origin-Opener-Policy", "same-origin");
        return new Response(response.body, {
            status: response.status,
            statusText: response.statusText,
            headers: newHeaders,
        });
    };

    self.addEventListener("install", () => self.skipWaiting());
    self.addEventListener("activate", (event) => event.waitUntil(
        // GS_: 认领 + 清理旧版本缓存（保留当前版本键）
        self.clients.claim().then(() => caches.keys().then((keys) =>
            Promise.all(keys
                .filter((k) => k.startsWith("gs-web-") && k !== GS_APP_CACHE)
                .map((k) => caches.delete(k)))))
    ));

    self.addEventListener("message", (ev) => {
        if (!ev.data) {
            return;
        } else if (ev.data.type === "deregister") {
            self.registration
                .unregister()
                .then(() => {
                    return self.clients.matchAll();
                })
                .then(clients => {
                    clients.forEach((client) => client.navigate(client.url));
                });
        } else if (ev.data.type === "coepCredentialless") {
            coepCredentialless = ev.data.value;
        }
    });

    self.addEventListener("fetch", function (event) {
        const r = event.request;
        if (r.cache === "only-if-cached" && r.mode !== "same-origin") {
            return;
        }

        const request = (coepCredentialless && r.mode === "no-cors")
            ? new Request(r, {
                credentials: "omit",
            })
            : r;

        // GS_: 核心产物 cache-first（命中直接回放补头后的响应；miss 拉取，
        // 原始响应入缓存、补头响应返回页面）。导航/manifest/模型等仍走
        // 下方的通用 passthrough。
        if (r.method === "GET" && gsCacheable(new URL(r.url))) {
            event.respondWith(
                caches.open(GS_APP_CACHE)
                    .then((cache) => cache.match(r).then((hit) => {
                        if (hit) return gsRewrite(hit);
                        return fetch(r).then((response) => {
                            if (response.status !== 200 || response.type !== "basic") {
                                return gsRewrite(response);
                            }
                            // GS_: 只缓存类型正确的 200——错误页/登录页/被代理
                            // 改写的 HTML 一旦以 200 落到 wasm 键位，会永久
                            // 回放成 "wasm 编译失败 + 无限加载页"。
                            const type = response.headers.get("content-type") || "";
                            const okType = /\.wasm$/.test(new URL(r.url).pathname)
                                ? /application\/wasm/i.test(type)
                                : /javascript/i.test(type);
                            if (!okType) return gsRewrite(response);
                            // 先 clone（tee）再取 body 构建 rewrite——
                            // body 被新 Response 接管后 clone 会抛
                            // "disturbed or locked"
                            const cached = response.clone();
                            const rewritten = gsRewrite(response);
                            event.waitUntil(cache.put(r, cached));
                            return rewritten;
                        });
                    }))
                    .catch((e) => console.error(e))
            );
            return;
        }

        event.respondWith(
            fetch(request)
                .then((response) => {
                    if (response.status === 0) {
                        return response;
                    }

                    const newHeaders = new Headers(response.headers);
                    newHeaders.set("Cross-Origin-Embedder-Policy",
                        coepCredentialless ? "credentialless" : "require-corp"
                    );
                    if (!coepCredentialless) {
                        newHeaders.set("Cross-Origin-Resource-Policy", "cross-origin");
                    }
                    newHeaders.set("Cross-Origin-Opener-Policy", "same-origin");

                    return new Response(response.body, {
                        status: response.status,
                        statusText: response.statusText,
                        headers: newHeaders,
                    });
                })
                .catch((e) => console.error(e))
        );
    });

} else {
    (() => {
        const reloadedBySelf = window.sessionStorage.getItem("coiReloadedBySelf");
        window.sessionStorage.removeItem("coiReloadedBySelf");
        const coepDegrading = (reloadedBySelf == "coepdegrade");

        // You can customize the behavior of this script through a global `coi` variable.
        const coi = {
            shouldRegister: () => !reloadedBySelf,
            shouldDeregister: () => false,
            coepCredentialless: () => true,
            coepDegrade: () => true,
            doReload: () => window.location.reload(),
            quiet: false,
            ...window.coi
        };

        const n = navigator;
        const controlling = n.serviceWorker && n.serviceWorker.controller;

        // Record the failure if the page is served by serviceWorker.
        if (controlling && !window.crossOriginIsolated) {
            window.sessionStorage.setItem("coiCoepHasFailed", "true");
        }
        const coepHasFailed = window.sessionStorage.getItem("coiCoepHasFailed");

        if (controlling) {
            // Reload only on the first failure.
            const reloadToDegrade = coi.coepDegrade() && !(
                coepDegrading || window.crossOriginIsolated
            );
            n.serviceWorker.controller.postMessage({
                type: "coepCredentialless",
                value: (reloadToDegrade || coepHasFailed && coi.coepDegrade())
                    ? false
                    : coi.coepCredentialless(),
            });
            if (reloadToDegrade) {
                !coi.quiet && console.log("Reloading page to degrade COEP.");
                window.sessionStorage.setItem("coiReloadedBySelf", "coepdegrade");
                coi.doReload("coepdegrade");
            }

            if (coi.shouldDeregister()) {
                n.serviceWorker.controller.postMessage({ type: "deregister" });
            }
        }

        // If we're already coi: do nothing. Perhaps it's due to this script doing its job, or COOP/COEP are
        // already set from the origin server. Also if the browser has no notion of crossOriginIsolated, just give up here.
        if (window.crossOriginIsolated !== false || !coi.shouldRegister()) return;

        if (!window.isSecureContext) {
            !coi.quiet && console.log("COOP/COEP Service Worker not registered, a secure context is required.");
            return;
        }

        // In some environments (e.g. Firefox private mode) this won't be available
        if (!n.serviceWorker) {
            !coi.quiet && console.error("COOP/COEP Service Worker not registered, perhaps due to private mode.");
            return;
        }

        n.serviceWorker.register(window.document.currentScript.src).then(
            (registration) => {
                !coi.quiet && console.log("COOP/COEP Service Worker registered", registration.scope);

                registration.addEventListener("updatefound", () => {
                    !coi.quiet && console.log("Reloading page to make use of updated COOP/COEP Service Worker.");
                    window.sessionStorage.setItem("coiReloadedBySelf", "updatefound");
                    coi.doReload();
                });

                // If the registration is active, but it's not controlling the page
                if (registration.active && !n.serviceWorker.controller) {
                    !coi.quiet && console.log("Reloading page to make use of COOP/COEP Service Worker.");
                    window.sessionStorage.setItem("coiReloadedBySelf", "notcontrolling");
                    coi.doReload();
                }
            },
            (err) => {
                !coi.quiet && console.error("COOP/COEP Service Worker failed to register:", err);
            }
        );
    })();
}
