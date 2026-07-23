if (window.location.pathname.endsWith(".html")) {
  var newUrl = window.location.pathname.replace(".html", "");
  window.history.replaceState(null, null, newUrl);
}

if (window.location.pathname.endsWith("index")) {
  var newUrl = window.location.pathname.replace("index", "");
  window.history.replaceState(null, null, newUrl);
}